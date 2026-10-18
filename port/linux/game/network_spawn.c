/*
NETWORK_SPAWN.C

The spawn menu in a network game (network_spawn.h), a fork extension:
offered only to machines that took it (network_extensions.c), in messages
of a type of its own, all reliable.

- A client's request (_network_spawn_request): the thing (its group, path
  and the map it is loaded from, with that map's checksum) and where the
  client's view looks (its camera's place and direction). The host checks
  what it can (the strings, the numbers, how often the machine asks) and
  spawns it as its own spawn menu does (spawn_menu.c), on the surface that
  view finds, ignoring the client's own unit and vehicle.
- The host's load (_network_spawn_load): tags it loaded from another map
  into the playing map, at which places, and for what (spawn_tags.h). Each
  client loads the same from its own copy of that map (of the same
  checksum), at the same places, in the order told, before the object that
  needs them, which the host tells of after it; a client that joins later
  is told of every load so far. A client that could not load them yet (its
  copy of the map is being decompressed) asks the host for its objects
  again once it has, for those it could not make meanwhile.
- The host's answer to a request (_network_spawn_result): what the menu's
  line beneath says.
*/

/* ---------- headers */

#include "cseries.h"
#include "game/game.h"
#include "interface/ui_widget.h"
#include "networking/network_game_globals.h"
#include "network_distributed.h"
#include "network_extensions.h"
#include "network_spawn.h"
#include "spawn_menu.h"
#include "spawn_tags.h"

#include "math/real_math.h"

#include <math.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the messages' type (spray_share.h's is 250) */
	NETWORK_SPAWN_MESSAGE_TYPE = 251,
	NETWORK_SPAWN_MAGIC = 'HSPN',

	_network_spawn_request = 1,
	_network_spawn_load,
	_network_spawn_result,

	/* the loads a client has yet to make */
	MAXIMUM_PENDING_LOADS = 256,
	/* a machine's requests: one at least this often (milliseconds) */
	NETWORK_SPAWN_REQUEST_INTERVAL = 100,
	NETWORK_SPAWN_RESULT_LENGTH = 96,
};

/* (the world's extent the host takes a client's view from) */
#define NETWORK_SPAWN_WORLD_BOUND 10000.0f

/* ---------- structures */

struct network_spawn_message_header
{
	struct distributed_message_header header;
	unsigned long magic;
	byte kind;
	byte pad[3];
};

struct network_spawn_request_message
{
	struct network_spawn_message_header header;
	unsigned long group_tag;
	unsigned long source_checksum;
	char source[SPAWN_TAGS_MAP_NAME_LENGTH];
	char path[SPAWN_TAGS_PATH_LENGTH];
	real origin[3];
	real direction[3];
};

struct network_spawn_load_message
{
	struct network_spawn_message_header header;
	long first_index;
	long count;
	unsigned long group_tag;
	unsigned long source_checksum;
	char source[SPAWN_TAGS_MAP_NAME_LENGTH];
	char path[SPAWN_TAGS_PATH_LENGTH];
};

struct network_spawn_result_message
{
	struct network_spawn_message_header header;
	char text[NETWORK_SPAWN_RESULT_LENGTH];
};

/* ---------- prototypes */

unsigned long system_milliseconds(void);
void platform_log(char const *format, ...);
/* network_objects.c's: the host's objects asked for again */
void network_objects_client_ask_again(void);

/* ---------- globals */

static struct
{
	/* the loads a client was told of and has yet to make, and the map they
	are for */
	struct spawn_tags_load pending[MAXIMUM_PENDING_LOADS];
	short pending_count;
	long generation;
	unsigned long last_request[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
} network_spawn;

/* ---------- private code */

static void network_spawn_header(
	struct network_spawn_message_header *header,
	byte kind)
{
	memset(header, 0, sizeof(*header));
	header->header.header = 8;
	header->header.type = NETWORK_SPAWN_MESSAGE_TYPE;
	header->header.count = 1;
	header->header.game_time = game_time_get();
	header->magic = NETWORK_SPAWN_MAGIC;
	header->kind = kind;
}

static boolean network_spawn_terminated(
	char const *text,
	unsigned long size)
{
	return memchr(text, 0, size) != NULL && text[0];
}

static boolean network_spawn_in_game(
	void)
{
	return game_in_progress() && !main_menu_is_active();
}

/* the loads told of, made in order: one that waits on its map stops the rest */
static void network_spawn_make_loads(
	void)
{
	boolean loaded = FALSE;

	while (network_spawn.pending_count)
	{
		struct spawn_tags_load const *told = &network_spawn.pending[0];
		struct spawn_tags_load made;
		long tag_index;
		char message[96];
		short result = spawn_tags_request(told->group_tag, told->path, told->source, told->source_checksum,
			told->first_index, &tag_index, &made, message, sizeof(message));

		if (result == _spawn_tags_pending)
			break;
		if (result == _spawn_tags_failed)
		{
			platform_log("spawn menu: the host's %s cannot be loaded here: %s", told->path, message);
			spawn_menu_note(message);
		}
		else if (made.count && (made.first_index != told->first_index || made.count != told->count))
		{
			platform_log("spawn menu: the host's %s took %ld tags at %ld, here %ld at %ld", told->path, told->count,
				told->first_index, made.count, made.first_index);
		}
		else if (made.count)
		{
			loaded = TRUE;
		}
		memmove(&network_spawn.pending[0], &network_spawn.pending[1],
			(network_spawn.pending_count - 1) * sizeof(network_spawn.pending[0]));
		network_spawn.pending_count--;
	}
	/* (the host's objects of these tags that could not be made before) */
	if (loaded)
		network_objects_client_ask_again();
}

/* ---------- public code */

boolean network_spawn_client(
	void)
{
	return game_connection() == _game_connection_network_client;
}

boolean network_spawn_host_available(
	void)
{
	return network_spawn_client() && network_extensions_allows(NONE, NETWORK_SPAWN_MESSAGE_TYPE);
}

void network_spawn_update(
	void)
{
	long generation = spawn_tags_map_generation();

	if (generation != network_spawn.generation)
	{
		network_spawn.generation = generation;
		network_spawn.pending_count = 0;
		memset(network_spawn.last_request, 0, sizeof(network_spawn.last_request));
	}
	if (network_spawn_client() && network_spawn_in_game())
		network_spawn_make_loads();
	else
		network_spawn.pending_count = 0;
}

boolean network_spawn_handles_message(
	word const *message,
	word size)
{
	struct network_spawn_message_header header;

	if (size < sizeof(header))
		return FALSE;
	memcpy(&header, message, sizeof(header));

	return header.header.type == NETWORK_SPAWN_MESSAGE_TYPE && header.magic == NETWORK_SPAWN_MAGIC;
}

void network_spawn_handle_message(
	long machine_index,
	word const *message,
	word size)
{
	struct network_spawn_message_header header;

	if (!network_spawn_handles_message(message, size) || !network_spawn_in_game())
		return;
	memcpy(&header, message, sizeof(header));
	switch (header.kind)
	{
	case _network_spawn_request:
	{
		struct network_spawn_request_message request;
		struct spawn_tags_entry entry;
		real_point3d origin;
		real_vector3d direction;
		real length;
		unsigned long now = system_milliseconds();
		short index;

		if (game_connection() != _game_connection_network_server || size != sizeof(request) ||
			machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES)
		{
			return;
		}
		memcpy(&request, message, sizeof(request));
		if (!network_spawn_terminated(request.source, sizeof(request.source)) ||
			!network_spawn_terminated(request.path, sizeof(request.path)))
		{
			return;
		}
		for (index = 0; index < 3; index++)
		{
			if (!isfinite(request.origin[index]) || fabsf(request.origin[index]) > NETWORK_SPAWN_WORLD_BOUND ||
				!isfinite(request.direction[index]))
			{
				return;
			}
		}
		length = square_root(request.direction[0] * request.direction[0] +
			request.direction[1] * request.direction[1] + request.direction[2] * request.direction[2]);
		if (length < 0.5f || length > 1.5f)
			return;
		if (network_spawn.last_request[machine_index] &&
			now - network_spawn.last_request[machine_index] < NETWORK_SPAWN_REQUEST_INTERVAL)
		{
			network_spawn_send_result(machine_index, "Spawning too fast");
			return;
		}
		network_spawn.last_request[machine_index] = now ? now : 1;
		memset(&entry, 0, sizeof(entry));
		entry.group_tag = request.group_tag;
		entry.source_checksum = request.source_checksum;
		strcpy(entry.source, request.source);
		strcpy(entry.path, request.path);
		origin.x = request.origin[0];
		origin.y = request.origin[1];
		origin.z = request.origin[2];
		direction.i = request.direction[0] / length;
		direction.j = request.direction[1] / length;
		direction.k = request.direction[2] / length;
		spawn_menu_queue(machine_index, &entry, &origin, &direction);
		break;
	}
	case _network_spawn_load:
	{
		struct network_spawn_load_message load;
		struct spawn_tags_load *pending;

		if (!network_spawn_client() || machine_index != NONE || size != sizeof(load) ||
			network_spawn.pending_count >= MAXIMUM_PENDING_LOADS)
		{
			return;
		}
		memcpy(&load, message, sizeof(load));
		if (!network_spawn_terminated(load.source, sizeof(load.source)) ||
			!network_spawn_terminated(load.path, sizeof(load.path)) ||
			load.first_index < SPAWN_TAGS_FIRST_INDEX || load.count < 1 ||
			load.first_index > SPAWN_TAGS_LAST_INDEX + 1 - load.count)
		{
			return;
		}
		pending = &network_spawn.pending[network_spawn.pending_count++];
		memset(pending, 0, sizeof(*pending));
		pending->first_index = load.first_index;
		pending->count = load.count;
		pending->group_tag = load.group_tag;
		pending->source_checksum = load.source_checksum;
		strcpy(pending->source, load.source);
		strcpy(pending->path, load.path);
		network_spawn_make_loads();
		break;
	}
	case _network_spawn_result:
	{
		struct network_spawn_result_message result;

		if (!network_spawn_client() || machine_index != NONE || size != sizeof(result))
			return;
		memcpy(&result, message, sizeof(result));
		result.text[sizeof(result.text) - 1] = 0;
		spawn_menu_note(result.text);
		break;
	}
	}
}

void network_spawn_machine_ready(
	long machine_index)
{
	long index;

	if (game_connection() != _game_connection_network_server)
		return;
	for (index = 0; index < spawn_tags_load_count(); index++)
	{
		struct spawn_tags_load const *load = spawn_tags_load_get(index);
		struct network_spawn_load_message message;

		network_spawn_header(&message.header, _network_spawn_load);
		message.first_index = load->first_index;
		message.count = load->count;
		message.group_tag = load->group_tag;
		message.source_checksum = load->source_checksum;
		memset(message.source, 0, sizeof(message.source));
		memset(message.path, 0, sizeof(message.path));
		strcpy(message.source, load->source);
		strcpy(message.path, load->path);
		distributed_send_to_machine_reliably(machine_index, &message, NETWORK_SPAWN_MESSAGE_TYPE, 1,
			(word)sizeof(message));
	}
}

boolean network_spawn_request(
	struct spawn_tags_entry const *entry,
	real_point3d const *origin,
	real_vector3d const *direction)
{
	struct network_spawn_request_message message;

	if (!network_spawn_host_available())
		return FALSE;
	network_spawn_header(&message.header, _network_spawn_request);
	message.group_tag = entry->group_tag;
	message.source_checksum = entry->source_checksum;
	memset(message.source, 0, sizeof(message.source));
	memset(message.path, 0, sizeof(message.path));
	strcpy(message.source, entry->source);
	strcpy(message.path, entry->path);
	message.origin[0] = origin->x;
	message.origin[1] = origin->y;
	message.origin[2] = origin->z;
	message.direction[0] = direction->i;
	message.direction[1] = direction->j;
	message.direction[2] = direction->k;
	distributed_send(&message, NETWORK_SPAWN_MESSAGE_TYPE, 1, (word)sizeof(message), _distributed_to_host_reliably);

	return TRUE;
}

void network_spawn_send_load(
	struct spawn_tags_load const *load)
{
	struct network_spawn_load_message message;

	if (game_connection() != _game_connection_network_server || !load->count)
		return;
	network_spawn_header(&message.header, _network_spawn_load);
	message.first_index = load->first_index;
	message.count = load->count;
	message.group_tag = load->group_tag;
	message.source_checksum = load->source_checksum;
	memset(message.source, 0, sizeof(message.source));
	memset(message.path, 0, sizeof(message.path));
	strcpy(message.source, load->source);
	strcpy(message.path, load->path);
	distributed_send(&message, NETWORK_SPAWN_MESSAGE_TYPE, 1, (word)sizeof(message), _distributed_to_clients_reliably);
}

void network_spawn_send_result(
	long machine_index,
	char const *text)
{
	struct network_spawn_result_message message;

	if (game_connection() != _game_connection_network_server)
		return;
	network_spawn_header(&message.header, _network_spawn_result);
	memset(message.text, 0, sizeof(message.text));
	strncpy(message.text, text, sizeof(message.text) - 1);
	distributed_send_to_machine_reliably(machine_index, &message, NETWORK_SPAWN_MESSAGE_TYPE, 1,
		(word)sizeof(message));
}
