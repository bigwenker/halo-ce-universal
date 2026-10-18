/*
SPAWN_MENU.C

The spawn menu: controls.spawn_menu (F6) opens a panel over the game with a
tab for each kind of thing there is, weapons, ammunition (and grenades),
equipment (powerups and health), characters (actor variants, with their AI,
a faction at a time: UNSC, Covenant, Flood, Forerunner) and vehicles, each a
box of its name in rows and columns (a weapon's and an
ammunition's with the icon of the weapon's pickup message, a grenade's with
its own). Its things are every map's (spawn_tags.c), the playing map's own
too; a thing the playing map has not is loaded into it, with what it needs,
from the map that has it. The key again closes the menu, as does the pause
key. While it is open the mouse is freed and is its pointer
(port/linux/src/sdl_platform.c), so the view stays still; the keyboard still
moves the player. A left click on a box makes one of it on the surface the
view looks at, as often as it is clicked; the menu stays open. The wheel
scrolls a tab that has more than fits.

What is made is the game's own object (or actor), made as the cheats make
theirs. In a network game the host makes it: a client's click asks the host
(network_spawn.c), which makes it where the client's view looks, and tells
every client of the tags it loaded for it.

debug.spawn_test spawns the things it names, one every few seconds, where
the player looks, as a click on each would ("wait" a turn without one,
"menu", "tab:<n>", "faction:<n>" and "shot" to open the menu, show a tab or
faction and save a screenshot of it): for automated tests.
*/

#include "cseries.h"
#include "spawn_menu.h"
#include "spawn_tags.h"
#include "network_spawn.h"
#include "halo_ui_pointer.h"
#include "halo_keyboard.h"
#include "halo_spray.h"
#include "ai/actor_definitions.h"
#include "ai/actor_placement.h"
#include "ai/ai_globals.h"
#include "ai/ai_scenario_definitions.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "cache/texture_cache.h"
#include "cseries/cseries_windows.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "interface/hud.h"
#include "interface/hud_definitions.h"
#include "interface/hud_draw.h"
#include "interface/interface.h"
#include "interface/ui_widget.h"
#include "main/console.h"
#include "math/real_math.h"
#include "networking/network_game_globals.h"
#include "network_social.h"
#include "objects/object_definitions.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "tag_files/tag_files.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/units.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* port_config.c's, and the platform's */
char const *config_string(char const *name);
void platform_log(char const *format, ...);
void platform_screenshot_request(void);

#define SPAWN_MENU_MAXIMUM_ITEMS 1024
#define SPAWN_MENU_NAME_LENGTH 64
/* the spawns waiting to be made */
#define SPAWN_MENU_MAXIMUM_JOBS 32
/* how far the view looks for a surface to spawn on (world units, 10 feet
each) */
#define SPAWN_MENU_RANGE 100.0f
/* how long the line beneath says what the last click did, and a box clicked
lights up */
#define SPAWN_MENU_MESSAGE_MS 3000
#define SPAWN_MENU_FLASH_MS 150
/* debug.spawn_test: a thing every so often */
#define SPAWN_MENU_TEST_INTERVAL_MS 4000
#define SPAWN_MENU_TEST_LENGTH 1024
/* the grenade hud interface's group, and where its messaging icon is in it
(hud_weapon.c's grenade_hud_interface_definition) */
#define SPAWN_MENU_GRENADE_HUD_TAG 'grhi'

/* the layout, in the screen's coordinates (480 high) */
#define SPAWN_MENU_MARGIN 28
#define SPAWN_MENU_MAXIMUM_WIDTH 820
#define SPAWN_MENU_PADDING 8
#define SPAWN_MENU_HEADER_HEIGHT 20
#define SPAWN_MENU_TAB_HEIGHT 22
#define SPAWN_MENU_FOOTER_HEIGHT 18
#define SPAWN_MENU_BOX_WIDTH 104
#define SPAWN_MENU_BOX_HEIGHT 68
#define SPAWN_MENU_BOX_GAP 6
#define SPAWN_MENU_BOX_INSET 4
#define SPAWN_MENU_SCROLLBAR_WIDTH 4
/* the lines a box's name takes at most, under an icon and alone */
#define SPAWN_MENU_ICON_LINES 2
#define SPAWN_MENU_MAXIMUM_LINES 4

static char const *const spawn_menu_tab_names[NUMBER_OF_SPAWN_TABS] =
{
	"Weapons", "Ammo", "Equipment", "NPCs", "Vehicles",
};

static char const *const spawn_menu_faction_names[NUMBER_OF_SPAWN_FACTIONS] =
{
	"UNSC", "Covenant", "Flood", "Forerunner",
};

/* the lists of things a grid shows: each tab's, but the NPCs', which are
those of each faction (after the tabs') */
#define SPAWN_MENU_LISTS (NUMBER_OF_SPAWN_TABS + NUMBER_OF_SPAWN_FACTIONS)

struct grenade_hud_interface_messaging
{
	byte unused0[0x1B8];
	struct tag_reference messaging_icon_bitmap;
	long unused1[12];
};

typedef char verify_grenade_hud_interface_messaging_size[sizeof(struct grenade_hud_interface_messaging) == 0x1F8 ? 1 : -1];

struct spawn_menu_item
{
	struct spawn_tags_entry entry;
	char name[SPAWN_MENU_NAME_LENGTH];
};

struct spawn_menu_job
{
	long machine_index;
	struct spawn_tags_entry entry;
	real_point3d origin;
	real_vector3d direction;
};

struct spawn_menu_layout
{
	rectangle2d panel;
	rectangle2d header;
	rectangle2d tabs[NUMBER_OF_SPAWN_TABS];
	/* the NPCs tab's factions, beneath the tabs (while it is shown) */
	rectangle2d factions[NUMBER_OF_SPAWN_FACTIONS];
	boolean has_factions;
	rectangle2d grid;
	rectangle2d footer;
	short columns;
	short rows;
	short box_width;
};

static struct
{
	boolean open;
	short tab;
	/* the NPCs tab's faction shown */
	short faction;
	/* the first row shown of each list */
	long scroll[SPAWN_MENU_LISTS];
	/* the things, sorted by tab, faction and name: each list's first and
	count, each tab's count; and whether they were all of them (the catalog
	was ready) */
	long count;
	long first[SPAWN_MENU_LISTS];
	long counts[SPAWN_MENU_LISTS];
	long tab_counts[NUMBER_OF_SPAWN_TABS];
	struct spawn_menu_item items[SPAWN_MENU_MAXIMUM_ITEMS];
	boolean items_complete;
	/* the item a click picked, spawned at the next update; NONE for none */
	long pending;
	long flash_item;
	unsigned long flash_at;
	char message[96];
	unsigned long message_at;
	/* the pointer: where it is, and the clicks and wheel since the menu was
	last drawn */
	boolean pointer_valid;
	short pointer_x, pointer_y;
	short clicks;
	short click_x, click_y;
	short wheel_steps;
	/* the spawns to make (this machine's, and the host's clients') */
	struct spawn_menu_job jobs[SPAWN_MENU_MAXIMUM_JOBS];
	short job_count;
	long generation;
	/* debug.spawn_test: what is left of it, and when the last went */
	char test[SPAWN_MENU_TEST_LENGTH];
	char const *test_next;
	boolean test_read;
	unsigned long test_at;
	boolean creating;
} spawn_menu = { FALSE, 0, 0, { 0 }, 0, { 0 }, { 0 }, { 0 }, { { { 0 } } }, FALSE, NONE, NONE };

/* ---------- the things */

static void spawn_menu_say(
	char const *text,
	char const *name)
{
	snprintf(spawn_menu.message, sizeof(spawn_menu.message), "%s%s", text, name ? name : "");
	spawn_menu.message_at = system_milliseconds();
	platform_log("spawn menu: %s", spawn_menu.message);
}

void spawn_menu_note(
	char const *text)
{
	spawn_menu_say(text, NULL);
}

boolean spawn_menu_creating_object(
	void)
{
	return spawn_menu.creating;
}

static int spawn_menu_item_compare(
	void const *a,
	void const *b)
{
	struct spawn_menu_item const *first = (struct spawn_menu_item const *)a;
	struct spawn_menu_item const *second = (struct spawn_menu_item const *)b;
	char const *x = first->name;
	char const *y = second->name;

	if (first->entry.tab != second->entry.tab)
		return first->entry.tab - second->entry.tab;
	if (first->entry.tab == _spawn_tab_characters && first->entry.faction != second->entry.faction)
		return first->entry.faction - second->entry.faction;
	while (*x && tolower((unsigned char)*x) == tolower((unsigned char)*y))
	{
		x++;
		y++;
	}
	if (tolower((unsigned char)*x) != tolower((unsigned char)*y))
		return tolower((unsigned char)*x) - tolower((unsigned char)*y);
	return strcmp(first->entry.path, second->entry.path);
}

static boolean spawn_menu_same_name(
	char const *a,
	char const *b)
{
	for (; *a && *b; a++, b++)
	{
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return FALSE;
	}
	return *a == *b;
}

static long spawn_menu_item_find(
	unsigned long group_tag,
	char const *path)
{
	long index;

	for (index = 0; index < spawn_menu.count; index++)
	{
		if ((!group_tag || spawn_menu.items[index].entry.group_tag == group_tag) &&
			spawn_menu_same_name(spawn_menu.items[index].entry.path, path))
		{
			return index;
		}
	}

	return NONE;
}

static void spawn_menu_add(
	struct spawn_tags_entry const *entry)
{
	struct spawn_menu_item *item;
	char const *slash = strrchr(entry->path, '\\');

	if (spawn_menu.count >= SPAWN_MENU_MAXIMUM_ITEMS || spawn_menu_item_find(entry->group_tag, entry->path) != NONE)
		return;
	item = &spawn_menu.items[spawn_menu.count++];
	item->entry = *entry;
	snprintf(item->name, sizeof(item->name), "%s", slash ? slash + 1 : entry->path);
}

/* a tab's list (an NPC's, its faction's) */
static short spawn_menu_list_of(
	short tab,
	short faction)
{
	return tab == _spawn_tab_characters ?
		(short)(NUMBER_OF_SPAWN_TABS + PIN(faction, 0, NUMBER_OF_SPAWN_FACTIONS - 1)) : tab;
}

/* the list the grid shows */
static short spawn_menu_list(
	void)
{
	return spawn_menu_list_of(spawn_menu.tab, spawn_menu.faction);
}

/* the catalog's things (all maps', when it is ready), and the playing map's
own that it has not */
static void spawn_menu_items_build(
	void)
{
	static struct spawn_tags_entry playing[512];
	long playing_count = spawn_tags_playing_map_entries(playing, NUMBEROF(playing));
	long index;
	short list;
	short tab;

	spawn_menu.count = 0;
	spawn_menu.items_complete = spawn_tags_catalog_ready(NULL);
	for (index = 0; spawn_menu.items_complete && index < spawn_tags_catalog_count(); index++)
		spawn_menu_add(spawn_tags_catalog_get(index));
	for (index = 0; index < playing_count; index++)
		spawn_menu_add(&playing[index]);

	qsort(spawn_menu.items, (size_t)spawn_menu.count, sizeof(spawn_menu.items[0]), spawn_menu_item_compare);
	for (list = 0; list < SPAWN_MENU_LISTS; list++)
	{
		spawn_menu.first[list] = 0;
		spawn_menu.counts[list] = 0;
	}
	for (tab = 0; tab < NUMBER_OF_SPAWN_TABS; tab++)
		spawn_menu.tab_counts[tab] = 0;
	for (index = spawn_menu.count - 1; index >= 0; index--)
	{
		struct spawn_tags_entry const *entry = &spawn_menu.items[index].entry;

		list = spawn_menu_list_of(entry->tab, entry->faction);
		spawn_menu.first[list] = index;
		spawn_menu.counts[list]++;
		spawn_menu.tab_counts[entry->tab]++;
	}
	spawn_menu.pending = NONE;
	spawn_menu.flash_item = NONE;
}

/* ---------- spawning */

/* where the ray finds a surface (the world's, or an object's but the
ignored unit's and its vehicle's): a point a little off it along its
normal, by about the thing's size; FALSE if it finds nothing near */
static boolean spawn_menu_target(
	long definition_index,
	real_point3d const *origin,
	real_vector3d const *direction,
	long ignore_unit_index,
	real_point3d *position,
	real_vector3d *forward)
{
	long ignore_index = ignore_unit_index;
	struct collision_result collision;
	real_vector3d ray;
	real lift = 0.1f;
	real length;

	if (ignore_unit_index != NONE && unit_get(ignore_unit_index)->object.parent_object_index != NONE)
		ignore_index = unit_get(ignore_unit_index)->object.parent_object_index;
	ray.i = direction->i * SPAWN_MENU_RANGE;
	ray.j = direction->j * SPAWN_MENU_RANGE;
	ray.k = direction->k * SPAWN_MENU_RANGE;
	if (!collision_test_vector(
		FLAG(_collision_test_front_facing_surfaces_bit) |
			_collision_test_environment_flags | _collision_test_objects_all_types_flags,
		origin, &ray, ignore_index, &collision))
	{
		return FALSE;
	}
	if (definition_index != NONE)
		lift = PIN(object_definition_get(definition_index)->object.bounding_radius, 0.1f, 1.5f);
	position->x = collision.point.x + collision.plane.n.i * lift;
	position->y = collision.point.y + collision.plane.n.j * lift;
	position->z = collision.point.z + collision.plane.n.k * lift;
	/* (level, the way the view faces) */
	forward->i = direction->i;
	forward->j = direction->j;
	forward->k = 0.0f;
	length = square_root(forward->i * forward->i + forward->j * forward->j);
	if (length < 0.001f)
	{
		forward->i = 1.0f;
		forward->j = 0.0f;
	}
	else
	{
		forward->i /= length;
		forward->j /= length;
	}
	return TRUE;
}

/* makes the thing (its tag loaded) where the ray finds a surface; what
came of it in message */
static void spawn_menu_place(
	struct spawn_tags_entry const *entry,
	long tag_index,
	real_point3d const *origin,
	real_vector3d const *direction,
	long ignore_unit_index,
	char *message,
	short message_size)
{
	real_point3d position;
	real_vector3d forward;
	boolean character = entry->group_tag == ACTOR_VARIANT_DEFINITION_TAG;
	boolean faces_player = character || entry->group_tag == 'vehi';
	char const *name = strrchr(entry->path, '\\') ? strrchr(entry->path, '\\') + 1 : entry->path;
	long unit_definition_index = character ?
		actor_variant_definition_get(tag_index)->unit_reference.index : tag_index;
	long result;

	if (character && (!ai_globals || !ai_globals->ai_initialized_for_map))
	{
		snprintf(message, message_size, "This map has no AI to spawn characters");
		return;
	}
	if (!spawn_menu_target(unit_definition_index, origin, direction, ignore_unit_index, &position, &forward))
	{
		snprintf(message, message_size, "Look at a surface to spawn on");
		return;
	}
	/* (characters and vehicles face the player, things lie the way they look) */
	if (faces_player)
	{
		forward.i = -forward.i;
		forward.j = -forward.j;
	}
	spawn_menu.creating = TRUE;
	if (character)
	{
		struct actor_starting_location location;

		csmemset(&location, 0, sizeof(location));
		location.position = position;
		location.facing = arctangent(forward.j, forward.i);
		location.cluster_index = NONE;
		location.command_list_index = NONE;
		result = actor_place(tag_index, NONE, NONE, &location, FALSE, 0);
	}
	else
	{
		struct object_placement_data data;

		object_placement_data_new(&data, tag_index, NONE);
		data.position = position;
		data.forward = forward;
		data.up.i = 0.0f;
		data.up.j = 0.0f;
		data.up.k = 1.0f;
		result = object_new(&data);
	}
	spawn_menu.creating = FALSE;
	snprintf(message, message_size, result != NONE ? "Spawned %s" : "Could not spawn %s here", name);
}

static void spawn_menu_report(
	long machine_index,
	char const *text)
{
	if (machine_index == NONE)
		spawn_menu_note(text);
	else
		network_spawn_send_result(machine_index, text);
}

void spawn_menu_queue(
	long machine_index,
	struct spawn_tags_entry const *entry,
	real_point3d const *origin,
	real_vector3d const *direction)
{
	struct spawn_menu_job *job;

	if (spawn_menu.job_count >= SPAWN_MENU_MAXIMUM_JOBS)
	{
		spawn_menu_report(machine_index, "Spawning too fast");
		return;
	}
	job = &spawn_menu.jobs[spawn_menu.job_count++];
	job->machine_index = machine_index;
	job->entry = *entry;
	job->origin = *origin;
	job->direction = *direction;
}

/* the spawns waiting, in order: one whose map is being read waits, and
those after it */
static void spawn_menu_make_jobs(
	void)
{
	while (spawn_menu.job_count)
	{
		struct spawn_menu_job *job = &spawn_menu.jobs[0];
		struct spawn_tags_load load;
		long tag_index;
		char message[96];
		short result;

		if (game_connection() == _game_connection_network_client ||
			game_connection() == _game_connection_film_playback)
		{
			spawn_menu.job_count = 0;
			return;
		}
		result = spawn_tags_request(job->entry.group_tag, job->entry.path, job->entry.source,
			job->entry.source_checksum, NONE, &tag_index, &load, message, sizeof(message));
		if (result == _spawn_tags_pending)
		{
			if (job->machine_index == NONE && strcmp(message, spawn_menu.message))
				spawn_menu_note(message);
			return;
		}
		if (result == _spawn_tags_loaded)
		{
			long ignore_unit_index = job->machine_index == NONE ? player_control_get_unit_index(0) :
				network_spray_unit((int)job->machine_index);

			/* (the clients load them before the object that needs them,
			which the host tells of after this) */
			if (load.count)
				network_spawn_send_load(&load);
			spawn_menu_place(&job->entry, tag_index, &job->origin, &job->direction, ignore_unit_index, message,
				sizeof(message));
		}
		spawn_menu_report(job->machine_index, message[0] ? message : "Could not spawn that");
		memmove(&spawn_menu.jobs[0], &spawn_menu.jobs[1], (spawn_menu.job_count - 1) * sizeof(spawn_menu.jobs[0]));
		spawn_menu.job_count--;
	}
}

/* a click on the item: spawned where this machine's view looks, by it, or
by the host it asks */
static void spawn_menu_pick(
	struct spawn_menu_item const *item)
{
	struct observer_result const *camera = observer_get_camera(0);

	if (!camera)
		return;
	if (network_spawn_client())
	{
		if (network_spawn_request(&item->entry, &camera->position, &camera->forward))
			spawn_menu_say("Asking the host for ", item->name);
		else
			spawn_menu_say("This host has no spawn menu", NULL);
		return;
	}
	if (game_connection() == _game_connection_film_playback)
		return;
	spawn_menu_queue(NONE, &item->entry, &camera->position, &camera->forward);
}

/* debug.spawn_test's next thing, when it is due */
static void spawn_menu_test_update(
	void)
{
	unsigned long now = system_milliseconds();
	char path[SPAWN_TAGS_PATH_LENGTH];
	unsigned int length;
	long item;

	if (!spawn_menu.test_read)
	{
		spawn_menu.test_read = TRUE;
		snprintf(spawn_menu.test, sizeof(spawn_menu.test), "%s", config_string("debug.spawn_test"));
		spawn_menu.test_next = spawn_menu.test;
		spawn_menu.test_at = now;
	}
	if (!spawn_menu.test_next || !*spawn_menu.test_next || now - spawn_menu.test_at < SPAWN_MENU_TEST_INTERVAL_MS ||
		!spawn_tags_catalog_ready(NULL) || player_control_get_unit_index(0) == NONE)
	{
		return;
	}
	spawn_menu.test_at = now;
	while (*spawn_menu.test_next == ' ' || *spawn_menu.test_next == ',')
		spawn_menu.test_next++;
	length = (unsigned int)strcspn(spawn_menu.test_next, ",");
	if (!length)
		return;
	snprintf(path, sizeof(path), "%.*s", (int)MIN(length, sizeof(path) - 1), spawn_menu.test_next);
	spawn_menu.test_next += length;
	/* ("wait": a turn without a thing; "menu" opens the menu or closes it,
	"tab:<n>" shows a tab, "faction:<n>" the NPCs' faction, "shot" saves a
	screenshot: the menu looked at) */
	if (spawn_menu_same_name(path, "wait"))
		return;
	if (spawn_menu_same_name(path, "menu"))
	{
		if (!spawn_menu.open)
			spawn_menu_items_build();
		spawn_menu.open = !spawn_menu.open;
		return;
	}
	if (!strncmp(path, "tab:", 4))
	{
		spawn_menu.tab = (short)PIN(atoi(path + 4), 0, NUMBER_OF_SPAWN_TABS - 1);
		return;
	}
	if (!strncmp(path, "faction:", 8))
	{
		spawn_menu.faction = (short)PIN(atoi(path + 8), 0, NUMBER_OF_SPAWN_FACTIONS - 1);
		return;
	}
	if (spawn_menu_same_name(path, "shot"))
	{
		platform_screenshot_request();
		return;
	}
	if (!spawn_menu.items_complete)
		spawn_menu_items_build();
	item = spawn_menu_item_find(0, path);
	if (item == NONE)
		platform_log("spawn menu: test: no %s", path);
	else
		spawn_menu_pick(&spawn_menu.items[item]);
}

/* ---------- opening, closing and the pointer */

boolean spawn_menu_is_open(
	void)
{
	return spawn_menu.open;
}

static void spawn_menu_close(
	void)
{
	spawn_menu.open = FALSE;
	spawn_menu.pending = NONE;
	spawn_menu.clicks = 0;
	spawn_menu.wheel_steps = 0;
	spawn_menu.pointer_valid = FALSE;
}

void spawn_menu_update(
	void)
{
	boolean requested = halo_spawn_menu_requested() != 0;
	struct halo_ui_pointer pointer;
	int pointer_state;
	boolean allowed = game_in_progress() && !main_menu_is_active() && !game_time_get_paused();

	if (spawn_menu.generation != spawn_tags_map_generation())
	{
		spawn_menu.generation = spawn_tags_map_generation();
		spawn_menu.job_count = 0;
		spawn_menu.count = 0;
		spawn_menu.items_complete = FALSE;
	}
	/* (hurried while the menu is open, or a test waits on the catalog) */
	spawn_tags_update(spawn_menu.open || (spawn_menu.test_next && *spawn_menu.test_next));
	network_spawn_update();
	if (spawn_menu.open && !allowed)
		spawn_menu_close();
	if (requested)
	{
		if (spawn_menu.open)
		{
			spawn_menu_close();
		}
		else if (allowed && !console_is_active() && !chat_is_active())
		{
			spawn_menu_items_build();
			spawn_menu.open = TRUE;
			spawn_menu.message[0] = 0;
		}
	}
	/* (the catalog came while the menu was open: its things) */
	if (spawn_menu.open && !spawn_menu.items_complete && spawn_tags_catalog_ready(NULL))
		spawn_menu_items_build();
	pointer_state = halo_spawn_menu_pointer_update(spawn_menu.open, &pointer);
	/* (no pointer to pick with: Android's) */
	if (pointer_state < 0 && spawn_menu.open)
		spawn_menu_close();
	if (pointer_state > 0)
	{
		spawn_menu.pointer_valid = TRUE;
		spawn_menu.pointer_x = pointer.x;
		spawn_menu.pointer_y = pointer.y;
		if (pointer.left_clicks)
		{
			spawn_menu.clicks += pointer.left_clicks;
			spawn_menu.click_x = pointer.click_x;
			spawn_menu.click_y = pointer.click_y;
		}
		spawn_menu.wheel_steps += pointer.wheel_steps;
	}
	if (spawn_menu.open && spawn_menu.pending != NONE)
	{
		if (spawn_menu.pending < spawn_menu.count)
			spawn_menu_pick(&spawn_menu.items[spawn_menu.pending]);
		spawn_menu.pending = NONE;
	}
	if (allowed)
		spawn_menu_test_update();
	spawn_menu_make_jobs();
}

/* ---------- drawing */

static void spawn_menu_rectangle(
	rectangle2d *rectangle,
	short x0,
	short y0,
	short x1,
	short y1)
{
	rectangle->x0 = x0;
	rectangle->y0 = y0;
	rectangle->x1 = x1;
	rectangle->y1 = y1;
}

static boolean spawn_menu_point_in(
	rectangle2d const *rectangle,
	short x,
	short y)
{
	return x >= rectangle->x0 && x < rectangle->x1 && y >= rectangle->y0 && y < rectangle->y1;
}

static void spawn_menu_layout_build(
	struct spawn_menu_layout *layout)
{
	rectangle2d const *window = &render.camera.window_bounds;
	short width = (short)MIN(window->x1 - window->x0 - 2 * SPAWN_MENU_MARGIN, SPAWN_MENU_MAXIMUM_WIDTH);
	short left = (short)((window->x0 + window->x1 - width) / 2);
	short top = (short)(window->y0 + SPAWN_MENU_MARGIN);
	short bottom = (short)(window->y1 - SPAWN_MENU_MARGIN);
	short grid_width;
	short tab;
	short y;

	spawn_menu_rectangle(&layout->panel, left, top, (short)(left + width), bottom);
	y = (short)(top + SPAWN_MENU_PADDING);
	spawn_menu_rectangle(&layout->header, (short)(left + SPAWN_MENU_PADDING), y,
		(short)(left + width - SPAWN_MENU_PADDING), (short)(y + SPAWN_MENU_HEADER_HEIGHT));
	y = layout->header.y1;
	for (tab = 0; tab < NUMBER_OF_SPAWN_TABS; tab++)
	{
		short span = layout->header.x1 - layout->header.x0;

		spawn_menu_rectangle(&layout->tabs[tab],
			(short)(layout->header.x0 + span * tab / NUMBER_OF_SPAWN_TABS + (tab ? 1 : 0)), y,
			(short)(layout->header.x0 + span * (tab + 1) / NUMBER_OF_SPAWN_TABS - (tab + 1 < NUMBER_OF_SPAWN_TABS ? 1 : 0)),
			(short)(y + SPAWN_MENU_TAB_HEIGHT));
	}
	y = (short)(y + SPAWN_MENU_TAB_HEIGHT);
	/* (the NPCs' factions, a row of their own) */
	layout->has_factions = spawn_menu.tab == _spawn_tab_characters;
	if (layout->has_factions)
	{
		short faction;
		short span = layout->header.x1 - layout->header.x0;

		y = (short)(y + 2);
		for (faction = 0; faction < NUMBER_OF_SPAWN_FACTIONS; faction++)
		{
			spawn_menu_rectangle(&layout->factions[faction],
				(short)(layout->header.x0 + span * faction / NUMBER_OF_SPAWN_FACTIONS + (faction ? 1 : 0)), y,
				(short)(layout->header.x0 + span * (faction + 1) / NUMBER_OF_SPAWN_FACTIONS -
					(faction + 1 < NUMBER_OF_SPAWN_FACTIONS ? 1 : 0)),
				(short)(y + SPAWN_MENU_TAB_HEIGHT));
		}
		y = (short)(y + SPAWN_MENU_TAB_HEIGHT);
	}
	y = (short)(y + SPAWN_MENU_PADDING);
	spawn_menu_rectangle(&layout->footer, layout->header.x0, (short)(bottom - SPAWN_MENU_PADDING - SPAWN_MENU_FOOTER_HEIGHT),
		layout->header.x1, (short)(bottom - SPAWN_MENU_PADDING));
	spawn_menu_rectangle(&layout->grid, layout->header.x0, y,
		(short)(layout->header.x1 - 2 * SPAWN_MENU_SCROLLBAR_WIDTH), (short)(layout->footer.y0 - SPAWN_MENU_PADDING));
	grid_width = layout->grid.x1 - layout->grid.x0;
	layout->columns = (short)MAX((grid_width + SPAWN_MENU_BOX_GAP) / (SPAWN_MENU_BOX_WIDTH + SPAWN_MENU_BOX_GAP), 1);
	layout->rows = (short)MAX((layout->grid.y1 - layout->grid.y0 + SPAWN_MENU_BOX_GAP) /
		(SPAWN_MENU_BOX_HEIGHT + SPAWN_MENU_BOX_GAP), 1);
	/* (the boxes stretched to fill the row) */
	layout->box_width = (short)MAX((grid_width - (layout->columns - 1) * SPAWN_MENU_BOX_GAP) / layout->columns, 1);
}

/* the box of the slot shown (counted from the first shown) */
static void spawn_menu_box(
	struct spawn_menu_layout const *layout,
	long slot,
	rectangle2d *box)
{
	short x = (short)(layout->grid.x0 + (slot % layout->columns) * (layout->box_width + SPAWN_MENU_BOX_GAP));
	short y = (short)(layout->grid.y0 + (slot / layout->columns) * (SPAWN_MENU_BOX_HEIGHT + SPAWN_MENU_BOX_GAP));

	spawn_menu_rectangle(box, x, y, (short)(x + layout->box_width), (short)(y + SPAWN_MENU_BOX_HEIGHT));
}

static long spawn_menu_maximum_scroll(
	struct spawn_menu_layout const *layout)
{
	long rows = (spawn_menu.counts[spawn_menu_list()] + layout->columns - 1) / layout->columns;

	return MAX(rows - layout->rows, 0);
}

/* the clicks and wheel since the menu was last drawn: a tab picked, a box
picked (spawned at the next update), the tab scrolled */
static void spawn_menu_input(
	struct spawn_menu_layout const *layout)
{
	long *scroll = &spawn_menu.scroll[spawn_menu_list()];

	*scroll -= spawn_menu.wheel_steps;
	spawn_menu.wheel_steps = 0;
	*scroll = PIN(*scroll, 0, spawn_menu_maximum_scroll(layout));
	if (spawn_menu.clicks > 0)
	{
		short tab;
		long slot;

		for (tab = 0; tab < NUMBER_OF_SPAWN_TABS; tab++)
		{
			if (spawn_menu_point_in(&layout->tabs[tab], spawn_menu.click_x, spawn_menu.click_y))
				spawn_menu.tab = tab;
		}
		for (tab = 0; layout->has_factions && tab < NUMBER_OF_SPAWN_FACTIONS; tab++)
		{
			if (spawn_menu_point_in(&layout->factions[tab], spawn_menu.click_x, spawn_menu.click_y))
				spawn_menu.faction = tab;
		}
		scroll = &spawn_menu.scroll[spawn_menu_list()];
		*scroll = PIN(*scroll, 0, spawn_menu_maximum_scroll(layout));
		for (slot = 0; slot < (long)layout->rows * layout->columns; slot++)
		{
			long position = *scroll * layout->columns + slot;
			rectangle2d box;

			if (position >= spawn_menu.counts[spawn_menu_list()])
				break;
			spawn_menu_box(layout, slot, &box);
			if (spawn_menu_point_in(&box, spawn_menu.click_x, spawn_menu.click_y))
			{
				spawn_menu.pending = spawn_menu.first[spawn_menu_list()] + position;
				spawn_menu.flash_item = spawn_menu.pending;
				spawn_menu.flash_at = system_milliseconds();
			}
		}
	}
	spawn_menu.clicks = 0;
}

static void spawn_menu_quad(
	rectangle2d const *rectangle,
	pixel32 color)
{
	rectangle2d drawn = *rectangle;

	offset_rectangle2d(&drawn, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
	draw_quad(&drawn, color);
}

/* text in the rectangle (clipped to it), justified 0 left, 1 right, 2 centre */
static void spawn_menu_text(
	long font_index,
	rectangle2d const *rectangle,
	short justification,
	real_argb_color const *color,
	char const *text)
{
	rectangle2d drawn = *rectangle;

	offset_rectangle2d(&drawn, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
	draw_string_set_draw_mode(font_index, NONE, justification, 0, color);
	rasterizer_draw_string(&drawn, &drawn, NULL, 0, text);
}

/* how wide the text is drawn (the draw mode's font) */
static short spawn_menu_text_width(
	char const *text)
{
	rectangle2d bounds, measured, end;

	bounds.x0 = bounds.y0 = 0;
	bounds.x1 = bounds.y1 = 30000;
	draw_string_compute_bounds(&bounds, text, &measured, &end);

	return end.x0;
}

/* the name, its words in lines no wider than width (a word wider than a
line broken where it must), at most maximum lines (the last cut short with
an ellipsis if there is more): returns how many */
static short spawn_menu_wrap(
	long font_index,
	char const *text,
	short width,
	short maximum,
	char lines[SPAWN_MENU_MAXIMUM_LINES][SPAWN_MENU_NAME_LENGTH])
{
	real_argb_color white = { 1.0f, 1.0f, 1.0f, 1.0f };
	short count = 0;

	draw_string_set_draw_mode(font_index, NONE, 0, 0, &white);
	while (*text == ' ')
		text++;
	while (*text && count < maximum)
	{
		char *line = lines[count];
		unsigned int length = 0;

		line[0] = 0;
		/* (whole words while they fit) */
		while (*text)
		{
			char candidate[SPAWN_MENU_NAME_LENGTH];
			unsigned int word = (unsigned int)strcspn(text, " ");

			snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, length ? " " : "", (int)word, text);
			if (strlen(candidate) >= sizeof(candidate) - 1 || spawn_menu_text_width(candidate) > width)
				break;
			strcpy(line, candidate);
			length = (unsigned int)strlen(line);
			text += word;
			while (*text == ' ')
				text++;
		}
		/* (a word alone wider than the line: as much of it as fits, to its
		last underscore or hyphen that does, if it has one) */
		if (!length)
		{
			unsigned int joint = 0;

			while (text[length] && text[length] != ' ' && length < SPAWN_MENU_NAME_LENGTH - 2)
			{
				line[length] = text[length];
				line[length + 1] = 0;
				if (length && spawn_menu_text_width(line) > width)
				{
					line[length] = 0;
					break;
				}
				if (text[length] == '_' || text[length] == '-')
					joint = length + 1;
				length++;
			}
			if (joint && text[length] && text[length] != ' ')
			{
				length = joint;
				line[length] = 0;
			}
			text += MAX(length, 1);
			while (*text == ' ')
				text++;
		}
		count++;
	}
	/* (more than fits: the last line ends with an ellipsis) */
	if (*text && count)
	{
		char *line = lines[count - 1];
		unsigned int length = (unsigned int)strlen(line);
		char candidate[SPAWN_MENU_NAME_LENGTH];

		for (;;)
		{
			snprintf(candidate, sizeof(candidate), "%.*s...", (int)length, line);
			if (!length || spawn_menu_text_width(candidate) <= width)
				break;
			length--;
		}
		strcpy(line, candidate);
	}

	return count;
}

/* the bitmap's sprite fitted to the area and centred in it */
static void spawn_menu_bitmap(
	struct bitmap_data const *bitmap,
	real_rectangle2d const *clip,
	rectangle2d const *area)
{
	real width = bitmap->width * (clip->x1 - clip->x0);
	real height = bitmap->height * (clip->y1 - clip->y0);
	real scale, center_x, center_y;
	point2d point;

	if (width <= 0.0f || height <= 0.0f)
		return;
	scale = MIN((area->x1 - area->x0) / width, (area->y1 - area->y0) / height);
	scale = MIN(scale, 2.0f);
	center_x = (area->x0 + area->x1) * 0.5f - render.camera.viewport_bounds.x0;
	center_y = (area->y0 + area->y1) * 0.5f - render.camera.viewport_bounds.y0;
	point.x = (short)(center_x - (bitmap->width * (clip->x0 + clip->x1) * 0.5f - bitmap->registration_point.x) * scale);
	point.y = (short)(center_y - (bitmap->height * (clip->y0 + clip->y1) * 0.5f - bitmap->registration_point.y) * scale);
	interface_draw_bitmap(bitmap, &point, clip, scale, 0.0f, 1.0f);
}

/* the bitmap group's sequence (or its first bitmap, if it has none):
whether there is one to draw */
static boolean spawn_menu_bitmap_group(
	long group_index,
	short sequence_index,
	rectangle2d const *area,
	boolean draw)
{
	real_rectangle2d whole = { 0.0f, 1.0f, 0.0f, 1.0f };
	struct bitmap_data const *bitmap = NULL;
	real_rectangle2d const *clip = NULL;

	if (group_index == NONE)
		return FALSE;
	hud_retrieve_bitmap_and_bounding_rect(group_index, sequence_index, 0, &bitmap, &clip);
	if (!bitmap)
	{
		struct bitmap_group *group = bitmap_group_get(group_index);

		if (sequence_index || group->bitmaps.count <= 0)
			return FALSE;
		bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, 0, struct bitmap_data);
		clip = NULL;
	}
	if (!clip)
		clip = &whole;
	/* (still loading: its place kept, drawn once it is in) */
	if (draw && _texture_cache_bitmap_get_hardware_format((struct bitmap_data *)bitmap, FALSE, TRUE))
		spawn_menu_bitmap(bitmap, clip, area);

	return TRUE;
}

/* the item's icon in the area: whether it has one */
static boolean spawn_menu_icon(
	struct spawn_tags_entry const *entry,
	rectangle2d const *area,
	boolean draw)
{
	switch (entry->icon_kind)
	{
	case _spawn_icon_message:
		if (!hud_globals || entry->icon_value == NONE)
			return FALSE;
		return spawn_menu_bitmap_group(hud_globals->messaging.messaging_icons.index, entry->icon_value, area, draw);

	case _spawn_icon_grenade:
	{
		struct game_globals *globals = scenario_get_game_globals();
		struct game_globals_grenade *grenade;
		struct grenade_hud_interface_messaging *hud;

		if (!globals || entry->icon_value < 0 || entry->icon_value >= globals->grenades.count)
			return FALSE;
		grenade = TAG_BLOCK_GET_ELEMENT(&globals->grenades, entry->icon_value, struct game_globals_grenade);
		if (grenade->hud_interface.index == NONE)
			return FALSE;
		hud = tag_get(SPAWN_MENU_GRENADE_HUD_TAG, grenade->hud_interface.index);
		return spawn_menu_bitmap_group(hud->messaging_icon_bitmap.index, 0, area, draw);
	}

	default:
		return FALSE;
	}
}

/* the key that closes the menu, as its binding names it ("F6") */
static void spawn_menu_key_name(
	char *name,
	unsigned int size)
{
	char const *binding = config_string("controls.spawn_menu");
	unsigned int length;

	while (*binding == ' ' || *binding == ',')
		binding++;
	length = (unsigned int)strcspn(binding, ",");
	if (length >= size)
		length = size - 1;
	memcpy(name, binding, length);
	name[length] = 0;
}

/* a box: its colour, its icon, its name in as many lines as it takes */
static void spawn_menu_draw_box(
	long font_index,
	short line_height,
	struct spawn_menu_item const *item,
	rectangle2d const *box,
	boolean hovered,
	boolean flashing)
{
	real_argb_color white = { 1.0f, 1.0f, 1.0f, 1.0f };
	real_argb_color dim = { 1.0f, 0.62f, 0.74f, 0.86f };
	char lines[SPAWN_MENU_MAXIMUM_LINES][SPAWN_MENU_NAME_LENGTH];
	short width = (short)(box->x1 - box->x0 - 2 * SPAWN_MENU_BOX_INSET);
	boolean icon = spawn_menu_icon(&item->entry, box, FALSE);
	short maximum = icon ? SPAWN_MENU_ICON_LINES :
		(short)PIN((SPAWN_MENU_BOX_HEIGHT - 2 * SPAWN_MENU_BOX_INSET) / MAX(line_height, 1), 1, SPAWN_MENU_MAXIMUM_LINES);
	short count = spawn_menu_wrap(font_index, item->name, width, maximum, lines);
	short text_height = (short)(count * line_height);
	short top;
	short line;

	spawn_menu_quad(box, flashing ? 0xF02896FFu : hovered ? 0xE0365474u : 0xC0222E3Cu);
	if (icon)
	{
		rectangle2d area = *box;

		area.x0 += 6;
		area.x1 -= 6;
		area.y0 += SPAWN_MENU_BOX_INSET;
		area.y1 = (short)(box->y1 - SPAWN_MENU_BOX_INSET - text_height - 2);
		if (area.y1 > area.y0)
			spawn_menu_icon(&item->entry, &area, TRUE);
		top = (short)(box->y1 - SPAWN_MENU_BOX_INSET - text_height);
	}
	else
	{
		top = (short)(box->y0 + (SPAWN_MENU_BOX_HEIGHT - text_height) / 2);
	}
	for (line = 0; line < count; line++)
	{
		rectangle2d bounds;

		spawn_menu_rectangle(&bounds, (short)(box->x0 + 1), (short)(top + line * line_height), (short)(box->x1 - 1),
			(short)MIN(top + (line + 1) * line_height + 2, box->y1));
		spawn_menu_text(font_index, &bounds, 2, hovered ? &white : &dim, lines[line]);
	}
}

void spawn_menu_draw(
	void)
{
	struct spawn_menu_layout layout;
	real_argb_color white = { 1.0f, 1.0f, 1.0f, 1.0f };
	real_argb_color dim = { 1.0f, 0.62f, 0.74f, 0.86f };
	real_argb_color accent = { 1.0f, 0.55f, 0.82f, 1.0f };
	unsigned long now = system_milliseconds();
	struct font_header *font;
	short line_height;
	long font_index;
	long scroll;
	long slot;
	short tab;
	real progress;
	char text[128];
	char key[32];

	if (!spawn_menu.open || !game_in_progress() || main_menu_is_active())
		return;
	font_index = interface_get_tag_index(_interface_font_terminal);
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	spawn_menu_layout_build(&layout);
	spawn_menu_input(&layout);
	scroll = spawn_menu.scroll[spawn_menu_list()];

	spawn_menu_quad(&layout.panel, 0xD80C141Cu);
	spawn_menu_key_name(key, sizeof(key));
	if (spawn_tags_catalog_ready(&progress))
		snprintf(text, sizeof(text), "SPAWN MENU");
	else
		snprintf(text, sizeof(text), "SPAWN MENU    reading the maps %d%%", (int)(progress * 100.0f));
	spawn_menu_text(font_index, &layout.header, 0, &accent, text);
	if (*key)
	{
		snprintf(text, sizeof(text), "%s: close", key);
		spawn_menu_text(font_index, &layout.header, 1, &dim, text);
	}

	for (tab = 0; tab < NUMBER_OF_SPAWN_TABS; tab++)
	{
		boolean hovered = spawn_menu.pointer_valid &&
			spawn_menu_point_in(&layout.tabs[tab], spawn_menu.pointer_x, spawn_menu.pointer_y);
		rectangle2d label = layout.tabs[tab];

		spawn_menu_quad(&layout.tabs[tab], tab == spawn_menu.tab ? 0xF02670B4u : hovered ? 0xE0344A60u : 0xC01E2A38u);
		label.y0 += 4;
		/* (its count too, where it fits) */
		snprintf(text, sizeof(text), "%s (%ld)", spawn_menu_tab_names[tab], spawn_menu.tab_counts[tab]);
		draw_string_set_draw_mode(font_index, NONE, 0, 0, &white);
		if (spawn_menu_text_width(text) > label.x1 - label.x0 - 4)
			snprintf(text, sizeof(text), "%s", spawn_menu_tab_names[tab]);
		spawn_menu_text(font_index, &label, 2, tab == spawn_menu.tab ? &white : &dim, text);
	}

	for (tab = 0; layout.has_factions && tab < NUMBER_OF_SPAWN_FACTIONS; tab++)
	{
		short list = spawn_menu_list_of(_spawn_tab_characters, tab);
		boolean hovered = spawn_menu.pointer_valid &&
			spawn_menu_point_in(&layout.factions[tab], spawn_menu.pointer_x, spawn_menu.pointer_y);
		rectangle2d label = layout.factions[tab];

		spawn_menu_quad(&layout.factions[tab], tab == spawn_menu.faction ? 0xE01F5C94u : hovered ? 0xD0304458u : 0xB0182430u);
		label.y0 += 4;
		snprintf(text, sizeof(text), "%s (%ld)", spawn_menu_faction_names[tab], spawn_menu.counts[list]);
		draw_string_set_draw_mode(font_index, NONE, 0, 0, &white);
		if (spawn_menu_text_width(text) > label.x1 - label.x0 - 4)
			snprintf(text, sizeof(text), "%s", spawn_menu_faction_names[tab]);
		spawn_menu_text(font_index, &label, 2, tab == spawn_menu.faction ? &white : &dim, text);
	}

	for (slot = 0; slot < (long)layout.rows * layout.columns; slot++)
	{
		long position = scroll * layout.columns + slot;
		long item_index = spawn_menu.first[spawn_menu_list()] + position;
		rectangle2d box;

		if (position >= spawn_menu.counts[spawn_menu_list()])
			break;
		spawn_menu_box(&layout, slot, &box);
		spawn_menu_draw_box(font_index, line_height, &spawn_menu.items[item_index], &box,
			spawn_menu.pointer_valid && spawn_menu_point_in(&box, spawn_menu.pointer_x, spawn_menu.pointer_y),
			spawn_menu.flash_item == item_index && now - spawn_menu.flash_at < SPAWN_MENU_FLASH_MS);
	}
	if (!spawn_menu.counts[spawn_menu_list()])
	{
		rectangle2d empty = layout.grid;

		empty.y0 += 8;
		spawn_menu_text(font_index, &empty, 2, &dim, spawn_menu.items_complete ? "None." : "Reading the maps...");
	}

	/* (where the scroll is, beside the rows, when they do not all fit) */
	if (spawn_menu_maximum_scroll(&layout) > 0)
	{
		long total = spawn_menu_maximum_scroll(&layout) + layout.rows;
		short height = layout.grid.y1 - layout.grid.y0;
		rectangle2d track, thumb;

		spawn_menu_rectangle(&track, (short)(layout.header.x1 - SPAWN_MENU_SCROLLBAR_WIDTH), layout.grid.y0,
			layout.header.x1, layout.grid.y1);
		thumb = track;
		thumb.y0 = (short)(layout.grid.y0 + height * scroll / total);
		thumb.y1 = (short)(layout.grid.y0 + height * (scroll + layout.rows) / total);
		spawn_menu_quad(&track, 0x80283444u);
		spawn_menu_quad(&thumb, 0xE02896FFu);
	}

	/* (what the last click did, else how to use it) */
	if (spawn_menu.message[0] && now - spawn_menu.message_at < SPAWN_MENU_MESSAGE_MS)
		spawn_menu_text(font_index, &layout.footer, 0, &white, spawn_menu.message);
	else
		spawn_menu_text(font_index, &layout.footer, 1, &dim, "left click: spawn    wheel: scroll");
}
