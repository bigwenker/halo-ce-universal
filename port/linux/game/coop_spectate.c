/*
COOP_SPECTATE.C

Spectating in network co-op. A dead player watches a living teammate
until they respawn (players.c respawns them beside a teammate once it is
safe). The campaign's dead camera would watch the body; instead this
follows a teammate from behind, and A (jump on the keyboard) switches to
the next one. director.c and hud.c call in here.

It also draws the line telling players about the vote to skip a cutscene
(network_coop.c counts the votes).
*/

#include "cseries.h"
#include "camera/dead_camera.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "input/input.h"
#include "interface/hud_messaging.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "units/units.h"

#include "coop_spectate.h"
#include "halo_keyboard.h"
#include "coop_respawn.h"
#include "network_coop.h"

#include <math.h>

/* ---------- constants */

/* camera distance behind the unit, pitch (radians), and how much of the
turn toward the unit's facing it makes each frame */
#define SPECTATE_DISTANCE 3.5f
#define SPECTATE_PITCH -0.3f
#define SPECTATE_TURN 0.15f

/* a passenger (riding a Pelican) is watched from in front of them, above
and looking down, so the camera stays inside the vehicle; a wide view (the
observer allows up to 90 degrees) takes in all of them from that close */
#define SPECTATE_PASSENGER_DISTANCE 1.0f
#define SPECTATE_PASSENGER_PITCH -0.5f
#define SPECTATE_PASSENGER_FIELD_OF_VIEW DEGREES_TO_RADIANS(85.0f)
/* field of view for every other spectate view (dead_camera.c's default) */
#define SPECTATE_FIELD_OF_VIEW DEGREES_TO_RADIANS(70.0f)

/* a teammate the scripts hold (Pillar of Autumn's cryo tube) is watched from
in front, far enough back to see all of what holds them */
#define SPECTATE_HELD_DISTANCE 2.0f
#define SPECTATE_HELD_PITCH -0.1f

/* ---------- globals */

/* the player each local player is watching, or NONE */
static long coop_spectate_watched[MAXIMUM_LOCAL_PLAYERS] = { NONE, NONE, NONE, NONE };

/* input_abstraction.c: how many ticks the keyboard's jump key has been held */
byte input_abstraction_port_accept(short controller_index);

/* ---------- private code */

/* Full player and unit handles are checked before camera use. Player datum
   order also works for split-screen: one machine can own several players. */
static boolean spectate_player_alive(struct player_datum const *player)
{
	struct unit_datum *unit = player && !player->quit_out_of_game ? unit_try_and_get(player->unit_index) : NULL;
	return unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit);
}
static long cycle_living_player(long self, long after, short direction)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long best = NONE, wrap = NONE;
	long rank = after == NONE ? (direction > 0 ? -1 : 65536) : DATUM_INDEX_TO_ABSOLUTE_INDEX(after);
	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		if (iterator.datum_index == self || !spectate_player_alive(player)) continue;
		if (wrap == NONE || (direction > 0 ? slot < DATUM_INDEX_TO_ABSOLUTE_INDEX(wrap) :
			slot > DATUM_INDEX_TO_ABSOLUTE_INDEX(wrap))) wrap = iterator.datum_index;
		if ((direction > 0 ? slot > rank : slot < rank) &&
			(best == NONE || (direction > 0 ? slot < DATUM_INDEX_TO_ABSOLUTE_INDEX(best) :
				slot > DATUM_INDEX_TO_ABSOLUTE_INDEX(best)))) best = iterator.datum_index;
	}
	return best != NONE ? best : wrap;
}
static long next_living_player(long self, long after)
{
	return cycle_living_player(self, after, 1);
}
/* One change per physical press, even when a simulation tick spans frames.
   Prime the held state on a new death so a held fire button cannot cycle. */
static short direction_pressed(short controller_index, boolean initialize)
{
	static boolean held[MAXIMUM_GAMEPADS];
	static byte mouse_held[MAXIMUM_GAMEPADS];
	struct gamepad_state const *gamepad;
	boolean down, next, previous;
	byte mouse;
	if (controller_index < 0 || controller_index >= MAXIMUM_GAMEPADS) return 0;
	gamepad = input_get_gamepad_state(controller_index);
	down = (gamepad && gamepad->buttons[FIRST_GAMEPAD_ANALOG_BUTTON + _gamepad_analog_button_a] > 0) ||
		input_abstraction_port_accept(controller_index) > 0;
	mouse = halo_spectate_mouse_buttons(controller_index);
	next = (down && !held[controller_index]) || ((mouse & 1) && !(mouse_held[controller_index] & 1));
	previous = (mouse & 2) && !(mouse_held[controller_index] & 2);
	held[controller_index] = down; mouse_held[controller_index] = mouse;
	return initialize || (next && previous) ? 0 : previous ? -1 : next ? 1 : 0;
}

/* whether the unit rides in a vehicle it isn't driving */
static boolean unit_is_passenger(
	long unit_index)
{
	long vehicle_index = object_get(unit_index)->object.parent_object_index;

	return vehicle_index != NONE && object_try_and_get_and_verify_type(vehicle_index, _object_mask_unit) &&
		unit_get(vehicle_index)->unit.driver_object_index != unit_index;
}

/* ---------- public code */

boolean coop_spectating(
	void)
{
	return network_coop_active();
}

long coop_spectate_unit(
	short local_player_index)
{
	static long last_self[MAXIMUM_LOCAL_PLAYERS], last_corpse[MAXIMUM_LOCAL_PLAYERS];
	long self, *watched;
	struct player_datum *player;
	short direction;
	boolean initialize;
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS) return NONE;
	self = local_player_get_player_index(local_player_index);
	player = player_try_and_get(self);
	if (!player) return NONE;
	watched = &coop_spectate_watched[local_player_index];
	initialize = last_self[local_player_index] != self || last_corpse[local_player_index] != player->dead_unit_index;
	last_self[local_player_index] = self; last_corpse[local_player_index] = player->dead_unit_index;
	direction = direction_pressed(player->network_player_data.controller_index, initialize);
	if (player->unit_index != NONE) return NONE;
	if (!spectate_player_alive(player_try_and_get(*watched)) || direction)
		*watched = cycle_living_player(self, *watched, direction < 0 ? -1 : 1);
	return *watched != NONE ? player_get(*watched)->unit_index : NONE;
}

boolean coop_spectate_watching_rider(
	short local_player_index)
{
	long self = local_player_get_player_index(local_player_index);
	long watched = coop_spectate_watched[local_player_index];
	long unit_index;
	long driver_index;

	if (self == NONE || player_get(self)->unit_index != NONE)
		return FALSE;
	/* the scripted camera may have been running since before this player
	died, so nobody has been picked to watch yet */
	if (!spectate_player_alive(player_try_and_get(watched)))
		watched = next_living_player(self, NONE);
	unit_index = watched != NONE ? player_get(watched)->unit_index : NONE;
	if (unit_index == NONE || !unit_is_passenger(unit_index))
		return FALSE;
	driver_index = unit_get(object_get(unit_index)->object.parent_object_index)->unit.driver_object_index;
	return driver_index == NONE || unit_get(driver_index)->unit.player_index == NONE;
}

boolean coop_spectate_nothing_to_watch(
	short local_player_index)
{
	long self = local_player_get_player_index(local_player_index);

	return self != NONE && player_get(self)->unit_index == NONE && next_living_player(self, NONE) == NONE;
}

void coop_spectate_camera(
	struct dead_camera *camera)
{
	struct object_datum *unit = camera->unit_index != NONE ? object_try_and_get(camera->unit_index) : NULL;
	real yaw, turn;

	if (!unit)
		return;
	yaw = (real)atan2(unit->object.forward.j, unit->object.forward.i);
	if (unit_is_passenger(camera->unit_index))
	{
		/* fixed in front of the seat, turning with the vehicle */
		camera->facing.yaw = yaw + _pi;
		camera->facing.pitch = SPECTATE_PASSENGER_PITCH;
		camera->distance = SPECTATE_PASSENGER_DISTANCE;
		camera->field_of_view = SPECTATE_PASSENGER_FIELD_OF_VIEW;
		return;
	}
	camera->field_of_view = SPECTATE_FIELD_OF_VIEW;
	if (!player_input_enabled())
	{
		camera->facing.yaw = yaw + _pi;
		camera->facing.pitch = SPECTATE_HELD_PITCH;
		camera->distance = SPECTATE_HELD_DISTANCE;
		return;
	}
	/* ease the camera round behind the unit */
	turn = yaw - camera->facing.yaw;
	while (turn > _pi)
		turn -= 2.0f * _pi;
	while (turn < -_pi)
		turn += 2.0f * _pi;
	camera->facing.yaw += turn * SPECTATE_TURN;
	camera->facing.pitch = SPECTATE_PITCH;
	camera->distance = SPECTATE_DISTANCE;
}

/* centred text near the bottom of the screen, in the HUD's font */
static void draw_bottom_text(
	wchar_t const *text)
{
	long font_index = hud_get_font_index();
	struct font_header *font;
	real_argb_color color = *global_real_argb_white;
	rectangle2d bounds;
	short height;

	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	height = (short)(font->ascending_height + font->descending_height);
	bounds = render.camera.window_bounds;
	bounds.y0 = (short)(bounds.y1 - 3 * height - 24);
	bounds.y1 = (short)(bounds.y1 - 24);
	/* draw mode 2: centered */
	draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}

void coop_spectate_draw(
	short local_player_index)
{
	long watched = coop_spectate_watched[local_player_index];
	wchar_t text[96];
	wchar_t const *hint;

	/* Timed deaths use only the normal multiplayer countdown HUD. */
	if (coop_respawn_active()) return;

	/* nobody to watch yet (still joining): network_coop.c shows the host's
	view from behind instead */
	if (watched == NONE || !player_try_and_get(watched))
	{
		if (coop_spectate_nothing_to_watch(local_player_index))
			draw_bottom_text(L"JOINING THE GAME\r\nYou spawn beside a teammate once it is safe");
		return;
	}
	if (!players_coop_waiting_to_start(local_player_get_player_index(local_player_index)))
		hint = L"You come back beside them once it is safe";
	else
		hint = L"You join them when they're on foot";
	usnprintf(text, NUMBEROF(text) - 1, L"SPECTATING %.12s   (A: NEXT)\r\n%s", player_get(watched)->name, hint);
	text[NUMBEROF(text) - 1] = 0;
	draw_bottom_text(text);
}

void coop_skip_vote_draw(
	short local_player_index)
{
	short votes, voters;
	boolean voted;
	wchar_t text[96];

	/* once, for the first local player */
	if (local_player_index != local_player_get_next(NONE) || !network_coop_skip_vote_status(&votes, &voters, &voted))
		return;
	if (voted)
		usnprintf(text, NUMBEROF(text) - 1, L"VOTED TO SKIP   %d OF %d", votes, voters);
	else
		usnprintf(text, NUMBEROF(text) - 1, L"PRESS SPACE OR A TO VOTE TO SKIP   %d OF %d", votes, voters);
	text[NUMBEROF(text) - 1] = 0;
	draw_bottom_text(text);
}
