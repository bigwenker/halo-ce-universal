/*
THIRD_PERSON.C

A third-person camera on foot, over the right shoulder. controls.third_person
(F5) cycles first person, rear view, front view and free look for the first local
player, in a campaign (co-op too), never in competitive multiplayer.

- The camera sits behind the player's head, a little to the right and up,
  looking where the player looks: the mouse turns the player as in first
  person. The = and - keys bring it nearer or further (THIRD_PERSON_NEAREST
  to THIRD_PERSON_FURTHEST). The wheel still changes weapons. The level and
  its objects push it in, as the game's own vehicle cameras are
  (observer.c's penetration check).
- The front view looks back at the player; input still turns and moves the
  player normally.
- Press F5 a third time to look freely: the player's facing, aim and head direction
  stay as they were at entry. The mouse turns the camera alone all the way
  round. Press F5 again to return to first person.
- On foot only: in a vehicle its own cameras, dead the game's dead camera, in
  a cutscene the cutscene's. The body is drawn and the first-person weapon
  is not (the director takes it as a third-person view: director.c).

Only the first local player's view and controls change; no new messages are sent.
*/

#include "cseries.h"
#include "real_math.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/hud_messaging.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/unit_control_data.h"

/* xinput_sdl.c's, port_config.c's */
void platform_third_person_active(int active);
int platform_third_person_distance(void);
int platform_third_person_key(void);

/* ---------- constants */

/* the camera behind the head, world units (a world unit about 3 m): nearest,
furthest and at first (about 0.75 m, 3.7 m and 1.7 m), and a key press */
#define THIRD_PERSON_NEAREST 0.25f
#define THIRD_PERSON_FURTHEST 1.2f
#define THIRD_PERSON_START 0.55f
#define THIRD_PERSON_NOTCH 0.08f
/* the camera over the right shoulder and a little up (about 40 cm, 15 cm) */
#define THIRD_PERSON_RIGHT 0.13f
#define THIRD_PERSON_UP 0.05f

/* ---------- globals */

enum
{
	_third_person_first,
	_third_person_follow,
	_third_person_front,
	_third_person_free,
	NUMBER_OF_THIRD_PERSON_MODES
};

static short third_person_mode = _third_person_first;
static real third_person_distance = THIRD_PERSON_START;
/* free look: the player's facing held while it lasts */
static boolean third_person_free_look = FALSE;
static boolean third_person_frozen = FALSE;
static real_euler_angles2d third_person_frozen_facing;
static real_vector3d third_person_frozen_looking;

/* ---------- private code */

/* the first local player's unit, alive and on foot (NONE: none) */
static long third_person_unit(
	void)
{
	long player_index = local_player_get_player_index(0);
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct unit_datum *unit;

	if (!player || player->unit_index == NONE)
		return NONE;
	unit = unit_try_and_get(player->unit_index);
	if (!unit || unit->object.parent_object_index != NONE ||
		TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
	{
		return NONE;
	}
	return player->unit_index;
}

/* ---------- public code */

/* A camera switch can bypass the on-foot eligibility check (a scripted
camera, for example). Release input without changing the user's toggle. */
void third_person_camera_deactivate(
	void)
{
	platform_third_person_active(FALSE);
	third_person_free_look = FALSE;
	third_person_frozen = FALSE;
}

/* (director.c, each frame) whether the local player's view is the
third-person camera now, its key read */
boolean third_person_camera_wanted(
	short local_player_index)
{
	boolean wanted;
	boolean changed;
	static wchar_t const *const messages[NUMBER_OF_THIRD_PERSON_MODES] =
	{
		L"Returned to first person",
		L"Entered third person",
		L"Entered front-facing third person",
		L"Entered free look"
	};

	if (local_player_index != 0)
		return FALSE;
	changed = platform_third_person_key() != 0;
	if (changed)
		third_person_mode = (third_person_mode + 1) % NUMBER_OF_THIRD_PERSON_MODES;
	/* (a campaign's, co-op's too: never in multiplayer, where a game
	engine runs) */
	wanted = !game_engine_running() && !cinematic_in_progress() && third_person_unit() != NONE;
	/* (zoomed in, a scope: first person at once, as the scope is the
	player's own view; back out again as it zooms out) */
	if (wanted)
	{
		struct unit_datum *unit = unit_try_and_get(third_person_unit());

		if (unit && unit->unit.current_zoom_level != NONE)
			wanted = FALSE;
	}
	/* Announce only a key-driven switch that can take effect on foot. */
	if (changed && wanted)
		hud_print_message(local_player_index, messages[third_person_mode]);
	wanted = wanted && third_person_mode != _third_person_first;
	platform_third_person_active(wanted);
	/* Capture the head once on entry, before the camera orbits behind it. */
	if (wanted && third_person_mode == _third_person_free && !third_person_free_look)
		unit_get_looking_vector(third_person_unit(), &third_person_frozen_looking);
	third_person_free_look = wanted && third_person_mode == _third_person_free;
	if (!third_person_free_look)
		third_person_frozen = FALSE;
	return wanted;
}

/* (director.c) the camera: a director_camera_update_proc */
void third_person_camera_update(
	void *camera,
	void *controls,
	void *result)
{
	struct observer_command *command = (struct observer_command *)result;
	short local_player_index = *(short const *)controls;
	long unit_index = third_person_unit();
	int distance = platform_third_person_distance();
	real_vector3d forward;

	(void)camera;
	if (unit_index == NONE)
		return;
	/* A distance key changes one step per press, independent of frame rate. */
	third_person_distance += distance * THIRD_PERSON_NOTCH;
	third_person_distance = PIN(third_person_distance, THIRD_PERSON_NEAREST, THIRD_PERSON_FURTHEST);

	player_control_get_facing_direction(local_player_index, &forward);
	if (normalize3d(&forward) <= 0.f)
		return;
	if (third_person_mode == _third_person_front)
	{
		/* yaw + pi and pitch negated: look back from the opposite side.
		Only the observer changes; the player's input and facing do not. */
		forward.i = -forward.i;
		forward.j = -forward.j;
		forward.k = -forward.k;
	}
	csmemset(command, 0, sizeof(*command));
	/* Keep focus distance and obstruction checks in observer_postcheck:
	walls shorten either view instead of hiding the player. */
	command->flags = FLAG(_observer_command_valid_bit);
	unit_get_camera_position(unit_index, &command->focus_position);
	/* (forward, right, up: observer_update's frame, by the heading) */
	command->focus_offset.i = 0.f;
	command->focus_offset.j = THIRD_PERSON_RIGHT;
	command->focus_offset.k = THIRD_PERSON_UP;
	command->focus_distance = third_person_distance;
	command->field_of_view = player_control_get_field_of_view(local_player_index);
	command->forward = forward;
	observer_up_from_forward(&command->forward, &command->up);
	object_get_velocities(unit_index, &command->focus_velocity, NULL);
}

/* Freeze the action before it enters prediction, history and the network.
The camera's desired angles remain local; every peer receives the same aim. */
void third_person_action_facing(
	short local_player_index,
	real_euler_angles2d *desired_facing)
{
	if (local_player_index != 0 || !third_person_free_look || third_person_unit() == NONE)
		return;
	if (!third_person_frozen)
	{
		third_person_frozen = TRUE;
		third_person_frozen_facing = *desired_facing;
	}
	*desired_facing = third_person_frozen_facing;
}

/* (players.c) the local player's control, as it goes to its unit: in free
look, the facing, aim and head direction held at their entry values */
void third_person_control(
	long player_index,
	real_euler_angles2d const *desired_facing,
	struct unit_control_data *control_data)
{
	struct player_datum *player = player_try_and_get(player_index);
	real_vector3d held;

	if (!player || player->local_player_index != 0 || !third_person_free_look)
		return;
	if (!third_person_frozen)
	{
		third_person_frozen = TRUE;
		third_person_frozen_facing = *desired_facing;
	}
	player_aiming_vector_from_facing(player_index, &held, &third_person_frozen_facing);
	control_data->looking_vector = third_person_frozen_looking;
	control_data->facing_vector = held;
	control_data->aiming_vector = held;
}
