/*
SPAWN_TAGS.H

The spawn menu's things from every map (port/linux/game/spawn_tags.c): a
catalog of the weapons, ammunition, equipment, characters and vehicles of
every Xbox map in the maps folder, and the loading of one into the map that
is playing, with the tags it needs, from the map that has it.
*/

#ifndef SPAWN_TAGS_H
#define SPAWN_TAGS_H

/* ---------- constants */

enum
{
	/* the longest tag path kept, with its terminator */
	SPAWN_TAGS_PATH_LENGTH = 128,
	/* the longest map name, with its terminator (a cache header's) */
	SPAWN_TAGS_MAP_NAME_LENGTH = 32,

	/* the tags loaded from other maps take the table's places from here on,
	the same on every machine of a network game whatever the map's own count,
	to the last place a tag index's 16 bits hold */
	SPAWN_TAGS_FIRST_INDEX = 0x4000,
	SPAWN_TAGS_LAST_INDEX = 0x7FFF,
};

/* the menu's tabs */
enum
{
	_spawn_tab_weapons,
	_spawn_tab_ammunition,
	_spawn_tab_equipment,
	_spawn_tab_characters,
	_spawn_tab_vehicles,
	NUMBER_OF_SPAWN_TABS
};

/* the NPCs tab's factions (a character's unit's team) */
enum
{
	_spawn_faction_unsc,
	_spawn_faction_covenant,
	_spawn_faction_flood,
	/* (the sentinels and the monitor) */
	_spawn_faction_forerunner,
	NUMBER_OF_SPAWN_FACTIONS
};

/* how an entry's box shows it */
enum
{
	_spawn_icon_none,
	/* a sequence of the hud globals' messaging icons (a weapon's pickup
	message's): value */
	_spawn_icon_message,
	/* the messaging icon of the game globals' grenade of type value */
	_spawn_icon_grenade,
};

/* spawn_tags_request's answers */
enum
{
	_spawn_tags_failed,
	/* its map is being made ready (decompressed): ask again later */
	_spawn_tags_pending,
	_spawn_tags_loaded,
};

/* ---------- structures */

struct spawn_tags_entry
{
	unsigned long group_tag;
	short tab;
	/* a character's faction (else 0) */
	short faction;
	short icon_kind;
	short icon_value;
	/* the map it is loaded from: the smallest that has it, or none (the
	playing map's own, a Custom Edition map's) */
	char source[SPAWN_TAGS_MAP_NAME_LENGTH];
	unsigned long source_checksum;
	char path[SPAWN_TAGS_PATH_LENGTH];
};

/* a load of tags into the playing map, as the host tells its clients and
a saved game remembers: the places they took, and what they were for */
struct spawn_tags_load
{
	long first_index;
	long count;
	unsigned long group_tag;
	char source[SPAWN_TAGS_MAP_NAME_LENGTH];
	unsigned long source_checksum;
	char path[SPAWN_TAGS_PATH_LENGTH];
};

/* ---------- prototypes/SPAWN_TAGS.C */

/* the maps scanned, a little each frame (more when hurried: the menu is
open), and a map being made ready for a load */
void spawn_tags_update(boolean hurry);
/* whether every map has been scanned; progress from 0 to 1 */
boolean spawn_tags_catalog_ready(real *progress);
long spawn_tags_catalog_count(void);
struct spawn_tags_entry const *spawn_tags_catalog_get(long index);
/* the entries of the playing map's own tags (its weapons and the rest),
for those of a map the catalog has not (a Custom Edition map's); count at
most, returns how many */
long spawn_tags_playing_map_entries(struct spawn_tags_entry *entries, long count);

/* the playing map's tag of that group and path (its own, or one loaded),
NONE if it has none */
long spawn_tags_find(unsigned long group_tag, char const *path);
/* loads the tag from the map source (of that checksum) into the playing
map, with every tag it needs that the playing map lacks, at first_index
(NONE: the next free place); its index in *tag_index, and what was loaded
in *load (count 0 when nothing needed loading) */
short spawn_tags_request(unsigned long group_tag, char const *path, char const *source,
	unsigned long source_checksum, long first_index, long *tag_index, struct spawn_tags_load *load,
	char *message, short message_size);
/* the loads since the playing map loaded, in order */
long spawn_tags_load_count(void);
struct spawn_tags_load const *spawn_tags_load_get(long index);

/* a read the cache makes of a loaded tag's pixels or samples, from its
map: FALSE if the tag is the playing map's own */
boolean spawn_tags_read(long tag_index, long offset, long size, void *buffer, boolean *failed);

/* a saved game: the loads it may need kept with it (z:\spawn\saved_<map>.txt),
and made again as it is loaded, in a game that has not made them yet; an
object of a loaded tag that is not there all the same is deleted
(game_state.c) */
void spawn_tags_before_save(void);
void spawn_tags_after_load(void);

/* which loading of a map's tags this is: it changes with each */
long spawn_tags_map_generation(void);

/* the playing map's tags loaded, and let go (cache_files.c) */
void spawn_tags_map_loaded(void);
void spawn_tags_map_unloaded(void);

#endif
