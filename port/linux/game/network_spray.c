#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "halo_spray.h"
#include "interface/ui_widget.h"
#include "memory/data.h"
#include "network_distributed.h"
#include "network_extensions.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

boolean network_distributed_client_send(void *, word);
boolean network_distributed_client_send_reliably(void *, word);
boolean network_distributed_server_send_to_machine(long, void *, word);
boolean network_distributed_server_send_to_machine_reliably(long, void *, word);
short network_distributed_server_machines(long *, short);
void posix_random_bytes(void *, unsigned int);
unsigned long system_milliseconds(void);

static struct spray_share *share;
static int host_role, local_machine;
static void *session;
static unsigned long last_spray[SPRAY_SHARE_PEERS];
static byte has_sprayed[SPRAY_SHARE_PEERS];

int network_spray_is_local(int owner)
{
	return share && owner == local_machine;
}

long network_spray_unit(int machine)
{
	struct data_iterator iterator;
	struct player_datum *player;
	if (!player_data || !game_in_progress())
		return NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		if (!player->quit_out_of_game && player->network_player_data.machine_index == machine)
			return distributed_living_unit(player);
	return NONE;
}

static int spray_send(void *context, int peer, const void *data, size_t size, int reliable)
{
	word buffer[(SPRAY_SHARE_PACKET + 1) / 2];
	(void)context;
	if (size > sizeof(buffer) || !network_extensions_allows(host_role ? peer : NONE, SPRAY_SHARE_TYPE))
		return 0;
	csmemcpy(buffer, data, size);
	build_message_header(&buffer[0], (word)size, 2, 0);
	if (host_role)
		return reliable
				   ? network_distributed_server_send_to_machine_reliably(peer, buffer, (word)size)
				   : network_distributed_server_send_to_machine(peer, buffer, (word)size);
	return reliable ? network_distributed_client_send_reliably(buffer, (word)size)
					: network_distributed_client_send(buffer, (word)size);
}

static int spray_accept(void *context, int machine, struct spray_pose *pose, char name[32])
{
	real_point3d position, origin;
	real_vector3d direction;
	struct collision_result collision;
	struct data_iterator iterator;
	struct player_datum *player;
	float length = 0, distance = 0;
	long unit;
	int i, n = 0;
	(void)context;
	if (machine < 0 || machine >= SPRAY_SHARE_PEERS || !game_in_progress() ||
		main_menu_is_active() || pose->bsp != global_structure_bsp_index_get() ||
		(has_sprayed[machine] &&
		 (unsigned long)game_time_get() - last_spray[machine] < 4 * TICKS_PER_SECOND))
		return 0;
	unit = network_spray_unit(machine);
	if (unit == NONE)
		return 0;
	object_get_origin(unit, &position);
	for (i = 0; i < 3; i++)
	{
		if (!isfinite(pose->origin[i]) || fabsf(pose->origin[i]) > 32768 ||
			!isfinite(pose->direction[i]))
			return 0;
		origin.n[i] = pose->origin[i];
		direction.n[i] = pose->direction[i];
		length += direction.n[i] * direction.n[i];
		distance += (origin.n[i] - position.n[i]) * (origin.n[i] - position.n[i]);
	}
	if (distance > 1.0f || length < 2.249f || length > 2.251f ||
		!collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit) |
								   _collision_test_environment_flags |
								   _collision_test_objects_all_types_flags,
							   &origin, &direction, unit, &collision) ||
		collision.type != _collision_result_structure)
		return 0;
	/* Profile names are taken from the host's player, never from a client's packet. */
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		if (!player->quit_out_of_game && player->network_player_data.machine_index == machine)
		{
			for (i = 0; i < 12 && player->name[i] && n < 31; i++)
			{
				unsigned int c = player->name[i];
				name[n++] = (char)((c >= 32 && c < 127) ? c : '_');
			}
			break;
		}
	name[n] = 0;
	return 1;
}

static int spray_ready(void *context, int slot, int owner, const void *data, size_t size,
					   const struct spray_pose *pose, const char *name, int local)
{
	(void)context;
	if (!network_spray_ready(slot, owner, data, size, pose, name, local))
		return 0;
	if (host_role && owner >= 0 && owner < SPRAY_SHARE_PEERS)
	{
		has_sprayed[owner] = 1;
		last_spray[owner] = (unsigned long)game_time_get();
	}
	return 1;
}

void network_spray_reset(void)
{
	spray_share_free(share);
	share = NULL;
	session = NULL;
	memset(has_sprayed, 0, sizeof(has_sprayed));
}

void network_spray_update(void)
{
	struct network_game_client *client = global_network_game_client_get();
	void *current = client ? network_game_client_get_game(client) : NULL;
	int host = global_network_game_server_get() != NULL;
	short machine = network_game_client_get_local_machine_index();
	long indices[SPRAY_SHARE_PEERS];
	byte present[SPRAY_SHARE_PEERS] = {0};
	int count, i;
	if (!current || machine == NONE || !game_in_progress() || main_menu_is_active() ||
		game_connection() == _game_connection_local ||
		(!host && !network_extensions_allows(NONE, SPRAY_SHARE_TYPE)))
	{
		if (share)
			network_spray_reset();
		return;
	}
	if (!share || current != session || host != host_role || machine != local_machine)
	{
		uint64_t nonce;
		struct spray_share_callbacks cb = {spray_send, spray_accept, spray_ready};
		network_spray_reset();
		posix_random_bytes(&nonce, sizeof(nonce));
		if (!nonce)
			nonce = 1;
		host_role = host;
		local_machine = machine;
		session = current;
		share = spray_share_new(host, nonce, cb, NULL);
		if (!share)
			return;
	}
	if (host)
	{
		count = network_distributed_server_machines(indices, SPRAY_SHARE_PEERS);
		for (i = 0; i < count; i++)
			if (indices[i] >= 0 && indices[i] < SPRAY_SHARE_PEERS &&
				network_extensions_allows(indices[i], SPRAY_SHARE_TYPE))
				present[indices[i]] = 1;
		for (i = 0; i < SPRAY_SHARE_PEERS; i++)
			spray_share_peer(share, i, present[i]);
	}
	else
		spray_share_peer(share, 0, 1);
	spray_share_update(share, (uint32_t)system_milliseconds());
}
void network_spray_machine_joined(long machine)
{
	if (machine >= 0 && machine < SPRAY_SHARE_PEERS)
	{
		has_sprayed[machine] = 0;
		if (share)
			spray_share_peer(share, (int)machine, 0);
	}
}
int network_spray_handles_message(const void *message, unsigned short size)
{
	return spray_share_handles(message, size);
}
void network_spray_handle_message(long machine, const void *message, unsigned short size)
{
	if (share)
		spray_share_receive(share, host_role ? (int)machine : 0, message, size);
}
int network_spray_publish(struct spray_pose pose)
{
	void *data;
	size_t size;
	int result;
	if (!spray_share_available(share) || !halo_spray_file_read(&data, &size))
		return 0;
	result = spray_share_publish(share, local_machine, data, size, pose, "");
	halo_spray_file_free(data);
	return result;
}
