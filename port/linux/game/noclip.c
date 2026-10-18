/*
NOCLIP.C

Noclip (noclip.h): controls.noclip (N) toggles it for the keyboard's player
(the first on this machine). A noclipping player on foot flies where they
look: forward and back along the view, up or down with it, sideways level;
jump rises and crouch sinks. They pass through everything, the level's
structure, its objects and the space outside it, at NOCLIP_SPEED, and stay
where they are when nothing is pressed. bipeds.c moves them so instead of
its physics (noclip_biped_move), so every machine flies a player who
noclips as the others do, from the input it has of them.

The host decides who may, and who does. Its own players may (all of them
in a game on one machine); a client's players may not until the host
allows them, picking Allow noclip in the scoreboard's menu of a player
(game_engine.c), which Forbid noclip takes back, landing them if they fly.
A client's key asks the host (_network_noclip_request), which flies the
player if they may; the host tells its clients who flies and who may
(_network_noclip_state), as it changes and every few seconds, and a client
that has just taken the extension at once. This is a fork extension
(message 252, network_extensions.c's feature bit 64), so it is offered
only to machines that took it; a host without it has no noclip for its
clients. The host takes a noclipping client's position (its prediction,
network_distributed.c) as fast as noclip flies, not as fast as a player
runs, and through anything.
*/

/* ---------- headers */

#include "cseries.h"
#include "noclip.h"
#include "halo_keyboard.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/hud_messaging.h"
#include "interface/ui_widget.h"
#include "main/console.h"
#include "math/real_math.h"
#include "networking/network_game_globals.h"
#include "network_distributed.h"
#include "network_extensions.h"
#include "network_social.h"
#include "objects/objects.h"
#include "units/bipeds.h"
#include "units/units.h"
#include <string.h>

/* ---------- constants */

enum
{
	/* the messages' type (network_spawn.c's is 251) */
	NETWORK_NOCLIP_MESSAGE_TYPE = 252,
	NETWORK_NOCLIP_MAGIC = 'HNCL',

	_network_noclip_request = 1,
	_network_noclip_state,

	/* a player's flags in the host's state */
	_noclip_active_bit = 0,
	_noclip_allowed_bit,

	/* the host's choice for a player: none (its own may, a client's not),
	allowed, forbidden */
	_noclip_override_none = 0,
	_noclip_override_allowed,
	_noclip_override_forbidden,

	/* how often the host tells its clients who flies when nothing changed,
	and how often it takes a machine's request (milliseconds) */
	NOCLIP_STATE_INTERVAL = 3000,
	NOCLIP_REQUEST_INTERVAL = 200,
};

/* how fast a noclipping player flies (world units a second): a little over
twice as fast as a player runs */
#define NOCLIP_SPEED 5.0f
/* how far from the world's middle a noclipping player may fly (world
units, within the netcode's bound of a unit's position) */
#define NOCLIP_WORLD_BOUND 4000.0f

/* ---------- structures */

struct network_noclip_message_header
{
	struct distributed_message_header header;
	unsigned long magic;
	byte kind;
	byte pad[3];
};

struct network_noclip_request_message
{
	struct network_noclip_message_header header;
	/* the player (its absolute index), and whether they would fly */
	byte player_index;
	byte active;
	byte pad[2];
};

struct network_noclip_state_message
{
	struct network_noclip_message_header header;
	/* each player's flags (_noclip_active_bit, _noclip_allowed_bit), by
	absolute index */
	byte players[MAXIMUM_TRACKED_PLAYERS];
};

/* ---------- prototypes */

unsigned long system_milliseconds(void);
char const *config_string(char const *name);
void platform_log(char const *format, ...);
/* network_distributed.c's: a player's prediction measured afresh on the
host (noclip begun or ended) */
void network_distributed_prediction_restart(short player_index);

/* ---------- globals */

static struct
{
	/* (the host, or a game on one machine) the player each entry is of (its
	identifier, 0 for none), the host's choice for them and whether they fly */
	short identifiers[MAXIMUM_TRACKED_PLAYERS];
	byte overrides[MAXIMUM_TRACKED_PLAYERS];
	boolean active[MAXIMUM_TRACKED_PLAYERS];
	/* each player's flags: on the host, as its clients were last told; on a
	client, as the host told */
	byte flags[MAXIMUM_TRACKED_PLAYERS];
	/* (the host) when it last told its clients, whether it has more to tell,
	and when it last took each machine's request */
	unsigned long last_state;
	boolean changed;
	unsigned long last_request[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	/* the game's time when last updated (a new game's is less) */
	long game_time;
} noclip;

/* ---------- private code */

static boolean noclip_in_game(
	void)
{
	return game_in_progress() && !main_menu_is_active();
}

static boolean noclip_client(
	void)
{
	return game_connection() == _game_connection_network_client;
}

static void noclip_header(
	struct network_noclip_message_header *header,
	byte kind)
{
	memset(header, 0, sizeof(*header));
	header->magic = NETWORK_NOCLIP_MAGIC;
	header->kind = kind;
}

/* the word to this machine's player (an absolute index) */
static void noclip_say(
	short player_index,
	char const *text)
{
	struct player_datum *player = distributed_player(player_index);
	wchar_t message[64];
	short index;

	if (!player || player->local_player_index == NONE)
		return;
	for (index = 0; text[index] && index < NUMBEROF(message) - 1; index++)
		message[index] = (wchar_t)(unsigned char)text[index];
	message[index] = 0;
	platform_log("noclip: player %d is told \"%s\"", player_index, text);
	hud_print_message(player->local_player_index, message);
}

/* each player's flags changed: what changed for this machine's own told */
static void noclip_note_flags(
	short player_index,
	byte before,
	byte after)
{
	if (after != before)
	{
		platform_log("noclip: player %d %s, %s", player_index,
			TEST_FLAG(after, _noclip_active_bit) ? "flies" : "does not fly",
			TEST_FLAG(after, _noclip_allowed_bit) ? "may" : "may not");
	}
	if (TEST_FLAG(after, _noclip_active_bit) != TEST_FLAG(before, _noclip_active_bit))
	{
		noclip_say(player_index, TEST_FLAG(after, _noclip_active_bit) ? "Noclip on" : "Noclip off");
	}
	else if (noclip_client() && TEST_FLAG(after, _noclip_allowed_bit) != TEST_FLAG(before, _noclip_allowed_bit))
	{
		noclip_say(player_index, TEST_FLAG(after, _noclip_allowed_bit) ? "The host allowed you noclip" :
			"The host forbade you noclip");
	}
}

/* (the host) whether the player may: the host's choice, else whether they
are its own */
static boolean noclip_host_allows(
	short player_index)
{
	struct player_datum *player = distributed_player(player_index);

	if (!player)
		return FALSE;
	if (noclip.overrides[player_index] == _noclip_override_allowed)
		return TRUE;
	if (noclip.overrides[player_index] == _noclip_override_forbidden)
		return FALSE;
	return player->local_player_index != NONE;
}

static void noclip_send_state(
	long machine_index)
{
	struct network_noclip_state_message message;

	noclip_header(&message.header, _network_noclip_state);
	memcpy(message.players, noclip.flags, sizeof(message.players));
	if (machine_index == NONE)
	{
		distributed_send(&message, NETWORK_NOCLIP_MESSAGE_TYPE, 1, (word)sizeof(message),
			_distributed_to_clients_reliably);
	}
	else
	{
		distributed_send_to_machine_reliably(machine_index, &message, NETWORK_NOCLIP_MESSAGE_TYPE, 1,
			(word)sizeof(message));
	}
}

/* (the host, or a game on one machine) each player's entry kept to who is
in the game and what they may, their flags made, and the clients told */
static void noclip_host_update(
	void)
{
	short player_index;
	unsigned long now = system_milliseconds();

	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct player_datum *player = distributed_player(player_index);
		short identifier = player ? player->identifier : 0;
		byte before = noclip.flags[player_index];
		byte after = 0;

		/* (a player new at the index: the host's choice for the one before
		is not theirs) */
		if (identifier != noclip.identifiers[player_index])
		{
			noclip.identifiers[player_index] = identifier;
			noclip.overrides[player_index] = _noclip_override_none;
			noclip.active[player_index] = FALSE;
		}
		/* (forbidden, or dead: a new life walks) */
		if (noclip.active[player_index] &&
			(!noclip_host_allows(player_index) || distributed_living_unit(player) == NONE))
		{
			noclip.active[player_index] = FALSE;
		}
		if (player)
		{
			SET_FLAG(after, _noclip_active_bit, noclip.active[player_index]);
			SET_FLAG(after, _noclip_allowed_bit, noclip_host_allows(player_index));
		}
		if (after != before)
		{
			noclip.flags[player_index] = after;
			noclip.changed = TRUE;
			noclip_note_flags(player_index, before, after);
			if (TEST_FLAG(after, _noclip_active_bit) != TEST_FLAG(before, _noclip_active_bit))
				network_distributed_prediction_restart(player_index);
		}
	}
	if (game_connection() == _game_connection_network_server &&
		(noclip.changed || now - noclip.last_state >= NOCLIP_STATE_INTERVAL))
	{
		noclip_send_state(NONE);
		noclip.changed = FALSE;
		noclip.last_state = now;
	}
}

/* (the host, or a game on one machine) the player would fly or land */
static void noclip_host_set(
	short player_index,
	boolean active)
{
	if (active && !noclip_host_allows(player_index))
		return;
	noclip.active[player_index] = active;
}

/* the key pressed: the keyboard's player (this machine's first) noclips or
stops, or asks the host to */
static void noclip_toggle(
	void)
{
	long player_datum_index = local_player_get_player_index(0);
	short player_index;
	struct player_datum *player;
	boolean active;

	if (player_datum_index == NONE || !(player = player_try_and_get(player_datum_index)) ||
		player->unit_index == NONE)
	{
		return;
	}
	player_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_datum_index);
	if (player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS)
		return;
	active = noclip_player_active(player_index);
	if (noclip_client())
	{
		struct network_noclip_request_message message;

		if (!network_extensions_allows(NONE, NETWORK_NOCLIP_MESSAGE_TYPE))
		{
			noclip_say(player_index, "The host does not have noclip");
			return;
		}
		if (!active && !TEST_FLAG(noclip.flags[player_index], _noclip_allowed_bit))
		{
			noclip_say(player_index, "The host has not allowed you noclip");
			return;
		}
		noclip_header(&message.header, _network_noclip_request);
		message.player_index = (byte)player_index;
		message.active = !active;
		distributed_send(&message, NETWORK_NOCLIP_MESSAGE_TYPE, 1, (word)sizeof(message),
			_distributed_to_host_reliably);
		return;
	}
	if (!active && !noclip_host_allows(player_index))
	{
		noclip_say(player_index, "Noclip is not allowed");
		return;
	}
	noclip_host_set(player_index, !active);
}

/* debug.test_input "noclip:<seed>": the keyboard's player starts noclip at
the start of every 20 seconds and stops it 12 seconds on, as the input's
pattern flies (xinput_sdl.c: the same clock), and the host allows every
player 30 seconds into the game */
static void noclip_test_update(
	void)
{
	static boolean checked;
	static boolean testing;
	static unsigned long last_toggle;
	long player_datum_index = local_player_get_player_index(0);
	unsigned long now = system_milliseconds();
	boolean wanted = now % 20000 < 12000;
	short player_index;

	if (!checked)
	{
		checked = TRUE;
		testing = !strncmp(config_string("debug.test_input"), "noclip:", 7);
	}
	if (!testing)
		return;
	if (game_connection() == _game_connection_network_server && game_time_get() > 30 * TICKS_PER_SECOND)
	{
		for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
		{
			if (distributed_player(player_index) && noclip.overrides[player_index] == _noclip_override_none &&
				!noclip_host_allows(player_index))
			{
				platform_log("noclip: the test allows player %d", player_index);
				noclip_host_allow(player_index, TRUE);
			}
		}
	}
	if (player_datum_index == NONE)
		return;
	player_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_datum_index);
	/* (a second for the host's answer) */
	if (noclip_player_active(player_index) != wanted && now - last_toggle >= 1000)
	{
		last_toggle = now;
		noclip_toggle();
	}
}

/* ---------- public code */

void noclip_update(
	void)
{
	boolean requested = halo_noclip_requested() != 0;
	long game_time = game_in_progress() ? game_time_get() : 0;

	/* (a new game: nobody flies, and the host's choices start afresh) */
	if (!noclip_in_game() || game_time < noclip.game_time)
	{
		memset(&noclip, 0, sizeof(noclip));
		if (!noclip_in_game())
			return;
	}
	noclip.game_time = game_time;
	if (requested && !game_time_get_paused() && !console_is_active() && !chat_is_active())
		noclip_toggle();
	noclip_test_update();
	if (!noclip_client())
		noclip_host_update();
}

boolean noclip_player_active(
	short player_index)
{
	if (player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS)
		return FALSE;
	if (noclip_client())
		return TEST_FLAG(noclip.flags[player_index], _noclip_active_bit);
	return noclip.active[player_index];
}

boolean noclip_unit_active(
	long unit_index)
{
	struct unit_datum *unit = unit_index != NONE ?
		(struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit) : NULL;

	return unit && unit->unit.player_index != NONE && unit->object.parent_object_index == NONE &&
		!TEST_FLAG(unit->object.damage_flags, _object_dead_bit) &&
		noclip_player_active((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(unit->unit.player_index));
}

real noclip_speed(
	void)
{
	return NOCLIP_SPEED / TICKS_PER_SECOND;
}

boolean noclip_biped_move(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	real_vector3d forward;
	real_vector3d facing;
	real_vector3d left;
	real_vector3d velocity;
	real_point3d position;
	real rise = 0.0f;
	real length;

	if (!noclip_unit_active(biped_index))
		return FALSE;
	/* (the body turned to the player's facing, level: in the air a biped
	turns only as its animations do, and its look is kept to where the body
	faces, so the view would turn away from where it flies) */
	facing = biped->unit.desired_facing_vector;
	facing.k = 0.0f;
	if (normalize3d(&facing) == 0.0f)
		facing = biped->object.forward;
	/* (forward and back along where the player looks, not where the body
	lets its head look; sideways level, at its left) */
	forward = biped->unit.desired_looking_vector;
	if (normalize3d(&forward) == 0.0f)
	{
		unit_get_looking_vector(biped_index, &forward);
		if (normalize3d(&forward) == 0.0f)
			forward = facing;
	}
	left.i = -forward.j;
	left.j = forward.i;
	left.k = 0.0f;
	if (normalize3d(&left) == 0.0f)
	{
		left.i = -facing.j;
		left.j = facing.i;
		left.k = 0.0f;
		normalize3d(&left);
	}
	if (TEST_FLAG(biped->unit.control_flags, _unit_control_jump_bit))
		rise += 1.0f;
	if (TEST_FLAG(biped->unit.control_flags, _unit_control_crouch_modifier_bit))
		rise -= 1.0f;
	velocity.i = forward.i * biped->unit.throttle.i + left.i * biped->unit.throttle.j;
	velocity.j = forward.j * biped->unit.throttle.i + left.j * biped->unit.throttle.j;
	velocity.k = forward.k * biped->unit.throttle.i + left.k * biped->unit.throttle.j + rise;
	/* (no faster diagonally; so written that a throttle not a number is
	none) */
	length = magnitude3d(&velocity);
	if (!(length <= 1.0f))
	{
		if (length > 1.0f)
			scale_vector3d(&velocity, 1.0f / length, &velocity);
		else
			velocity = *global_zero_vector3d;
	}
	scale_vector3d(&velocity, noclip_speed(), &velocity);
	position.x = biped->object.position.x + velocity.i;
	position.y = biped->object.position.y + velocity.j;
	position.z = biped->object.position.z + velocity.k;
	position.x = PIN(position.x, -NOCLIP_WORLD_BOUND, NOCLIP_WORLD_BOUND);
	position.y = PIN(position.y, -NOCLIP_WORLD_BOUND, NOCLIP_WORLD_BOUND);
	position.z = PIN(position.z, -NOCLIP_WORLD_BOUND, NOCLIP_WORLD_BOUND);
	object_set_position(biped_index, &position, &facing, global_up3d);
	biped->object.translational_velocity = velocity;
	biped->object.angular_velocity = *global_zero_vector3d;
	/* (in the air, as a biped falling is: it lands as one when noclip
	ends, and nothing it stood on carries it meanwhile) */
	SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);
	SET_FLAG(biped->biped.flags, _biped_slipping_bit, FALSE);
	biped->biped.airborne_ticks = 0;
	biped->biped.landing = NONE;
	biped->biped.support_surface_index = NONE;
	biped->biped.elevator_object_index = NONE;
	biped->biped.elevator_ticks = 0;
	biped->biped.pathfinding_point = position;
	biped->biped.pathfinding_surface_index = NONE;
	SET_FLAG(biped->object.flags, _object_on_ground_bit, FALSE);
	SET_FLAG(biped->object.flags, _object_at_rest_bit, FALSE);

	return TRUE;
}

boolean noclip_host(
	void)
{
	return game_connection() == _game_connection_network_server && noclip_in_game();
}

boolean noclip_player_allowed(
	short player_index)
{
	if (player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS)
		return FALSE;
	if (noclip_client())
		return TEST_FLAG(noclip.flags[player_index], _noclip_allowed_bit);
	return noclip_host_allows(player_index);
}

void noclip_host_allow(
	short player_index,
	boolean allowed)
{
	if (!noclip_host() || player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS ||
		!distributed_player(player_index))
	{
		return;
	}
	noclip.identifiers[player_index] = distributed_player(player_index)->identifier;
	noclip.overrides[player_index] = allowed ? _noclip_override_allowed : _noclip_override_forbidden;
	/* (told at once: the next frame's update) */
	noclip_host_update();
}

boolean network_noclip_handles_message(
	word const *message,
	word size)
{
	struct network_noclip_message_header header;

	if (size < sizeof(header))
		return FALSE;
	memcpy(&header, message, sizeof(header));

	return header.header.type == NETWORK_NOCLIP_MESSAGE_TYPE && header.magic == NETWORK_NOCLIP_MAGIC;
}

void network_noclip_handle_message(
	long machine_index,
	word const *message,
	word size)
{
	struct network_noclip_message_header header;

	if (!network_noclip_handles_message(message, size) || !noclip_in_game())
		return;
	memcpy(&header, message, sizeof(header));
	switch (header.kind)
	{
	case _network_noclip_request:
	{
		struct network_noclip_request_message request;
		unsigned long now = system_milliseconds();

		if (game_connection() != _game_connection_network_server || size != sizeof(request) ||
			machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES)
		{
			return;
		}
		memcpy(&request, message, sizeof(request));
		/* (a machine speaks for its own players only, a few times a second) */
		if (request.player_index >= MAXIMUM_TRACKED_PLAYERS ||
			!distributed_machine_has_player(machine_index, request.player_index) ||
			(noclip.last_request[machine_index] && now - noclip.last_request[machine_index] < NOCLIP_REQUEST_INTERVAL))
		{
			return;
		}
		noclip.last_request[machine_index] = now ? now : 1;
		noclip_host_set(request.player_index, request.active != 0);
		/* (told even when nothing changed: a request refused) */
		noclip.changed = TRUE;
		noclip_host_update();
		break;
	}
	case _network_noclip_state:
	{
		struct network_noclip_state_message state;
		short player_index;

		if (!noclip_client() || machine_index != NONE || size != sizeof(state))
			return;
		memcpy(&state, message, sizeof(state));
		for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
		{
			byte flags = (byte)(state.players[player_index] & (FLAG(_noclip_active_bit) | FLAG(_noclip_allowed_bit)));

			noclip_note_flags(player_index, noclip.flags[player_index], flags);
			noclip.flags[player_index] = flags;
		}
		break;
	}
	}
}

void network_noclip_machine_ready(
	long machine_index)
{
	if (game_connection() == _game_connection_network_server && noclip_in_game())
		noclip_send_state(machine_index);
}
