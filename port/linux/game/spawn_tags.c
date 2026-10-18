/*
SPAWN_TAGS.C

The spawn menu's things from every map (spawn_tags.h).

The catalog. Each Xbox map in the maps folder (but ui.map) is read once for
its tags: its weapons a player can carry (one with a first person model and
a HUD, not a vehicle's or a character's), its equipment (ammunition and
grenades, powerups and health), its characters (actor variants with a unit,
not a swarm's or a vehicle's pilot) and the vehicles a player can drive (one
with a driver's seat that is not a cutscene's prop: a lifepod, the fighter
bomber). A map is compressed on the
disc as on the Xbox (its header, then one zlib stream), so reading its tags
means inflating all of it; that is done a little each frame, and what each
map has is kept in z:\spawn\catalog.txt, read again while the map files are
the same (by size and header checksum). Each thing is loaded from the
smallest map that has it.

Loading a thing into the playing map. Its map is decompressed once into
z:\spawn\<map>.map (a little each frame; a map that is not compressed is
read where it is). Its tag data is read into memory of its own in the
contiguous window (the vertex buffers' data must be there,
port/linux/src/platform.h), and its pointers moved there by their groups'
schemas (tag_schema.h), and its model parts' vertex and index buffers too
(which are not fields); then the tags are checked by the validator as a map
is as it loads (tag_validate.c), its vertex and index buffers registered.
The thing's tag and every tag it needs, through its references and tag
indices, are added to the playing map's tag table at SPAWN_TAGS_FIRST_INDEX
on (the same places on every machine), its own map's versions of them all,
whatever the playing map has of the same names (which may be other versions
they do not fit), and their references made the playing map's. A tag of a
group with no schema is never loaded, nor one that is a map's own (a
scenario, a structure bsp, the globals): a reference to one is to the
playing map's of that name, or none. A loaded bitmap's pixels
and sound's samples are read from its map (spawn_tags_read).

Everything loaded is let go with the playing map's tags.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "cache/cache_files.h"
#include "cache/physical_memory_map.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "ai/actor_definitions.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "interface/weapon_hud_interface_definition.h"
#include "items/equipment_definitions.h"
#include "items/weapon_definitions.h"
#include "sound/sound_definitions.h"
#include "units/unit_definitions.h"
#include "units/vehicle_definitions.h"
#include "effects/contrails.h"
#include "effects/effects.h"
#include "effects/particle_systems.h"
#include "effects/particles.h"
#include "memory/data.h"
#include "main/main.h"
#include "objects/object_lights.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "sound/game_sound.h"
#include "tag_schema.h"
#include "spawn_tags.h"
/* (the port's zlib, as the cache's own decompression: cache_files_decompress_windows.c) */
#include "../../third_party/zlib/zlib_prefixed.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xtl.h>

/* ---------- constants */

#define SPAWN_TAGS_FOLDER "z:\\spawn"
#define SPAWN_TAGS_CATALOG "z:\\spawn\\catalog.txt"
#define SPAWN_TAGS_CATALOG_SIGNATURE "spawn catalog 3"
/* every Xbox map's tag data is linked to the Xbox tag cache's address */
#define SPAWN_TAGS_SOURCE_BASE 0x803A6000UL
#define SPAWN_TAGS_HEADER_SIGNATURE 'head'
#define SPAWN_TAGS_FOOTER_SIGNATURE 'foot'
#define SPAWN_TAGS_TAG_HEADER_SIGNATURE 'tags'
#define SPAWN_TAGS_CACHE_VERSION 5

enum
{
	MAXIMUM_SPAWN_SOURCES = 64,
	MAXIMUM_SPAWN_ENTRIES = 1024,
	MAXIMUM_SPAWN_LOADS = 256,
	SPAWN_CACHE_HEADER_SIZE = 0x800,
	/* how much is read and inflated at a time */
	SPAWN_INFLATE_CHUNK = 0x40000,
	/* how long a frame gives the scan and the decompression, idle and
	hurried (milliseconds) */
	SPAWN_IDLE_BUDGET = 3,
	SPAWN_HURRIED_BUDGET = 30,
	/* how deep a tag's blocks go (the validator's) */
	MAXIMUM_SPAWN_WALK_DEPTH = 32,
	/* the global tag map of a source tag not yet looked at, and of one
	being loaded */
	SPAWN_TAG_UNKNOWN = -2,
	SPAWN_TAG_PENDING = -3,
	/* (unit_definitions.h's seat flags: the second is locked) */
	SPAWN_SEAT_LOCKED_BIT = 1,
};

enum
{
	_source_unscanned,
	_source_scanning,
	_source_scanned,
	_source_failed,
};

enum
{
	_prepare_none,
	_prepare_inflating,
	_prepare_ready,
	_prepare_failed,
};

/* ---------- structures */

/* a cache file's header (cache_files.c's) */
struct spawn_cache_header
{
	unsigned long header_signature;
	long version;
	long file_length;
	byte reservedC[4];
	long tag_data_offset;
	long tag_data_size;
	byte reserved18[8];
	char name[0x20];
	char build[0x20];
	byte reserved60[4];
	unsigned long checksum;
	byte reserved68[0x794];
	unsigned long footer_signature;
};

/* a tag in a tag table (cache_files.c's cache_file_tag_instance) */
struct spawn_tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

/* a tag data's header (cache_files.c's cache_file_tag_header) */
struct spawn_tag_header
{
	struct spawn_tag_instance *instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	byte *vertex_buffers;
	long index_buffer_count;
	byte *index_buffers;
	unsigned long signature;
};

typedef char verify_spawn_cache_header_size[sizeof(struct spawn_cache_header) == SPAWN_CACHE_HEADER_SIZE ? 1 : -1];
typedef char verify_spawn_tag_instance_size[sizeof(struct spawn_tag_instance) == 0x20 ? 1 : -1];
typedef char verify_spawn_cache_header_checksum[offsetof(struct spawn_cache_header, checksum) == 0x64 ? 1 : -1];

struct spawn_source
{
	char name[SPAWN_TAGS_MAP_NAME_LENGTH];
	struct spawn_cache_header header;
	unsigned long file_size;
	boolean compressed;
	short scan_state;
	short prepare_state;
	/* the decompressed copy (or the map, if it is not compressed), open for
	reads, once ready */
	HANDLE prepared;
	/* the tags, loaded into the playing map's memory (NULL until a load
	from this map, and with the playing map) */
	byte *tags;
	boolean tags_failed;
	/* the playing map's tag index for each of these tags (SPAWN_TAG_UNKNOWN
	until looked at, NONE for none) */
	long *global_indices;
	long tag_count;
};

/* a thing a scanned map has */
struct spawn_scanned_entry
{
	short source;
	struct spawn_tags_entry entry;
};

/* a map being inflated: to scan its tags (capture), or to write it out
decompressed (output) */
struct spawn_inflater
{
	short source;
	boolean scanning;
	HANDLE input;
	HANDLE output;
	z_stream stream;
	boolean stream_open;
	byte *input_buffer;
	byte *output_buffer;
	/* the offset in the decompressed map of what comes out next, and where
	it stops */
	unsigned long position;
	unsigned long end;
	byte *capture;
	unsigned long capture_offset;
	unsigned long capture_size;
};

/* tags read from a map, for their things (the catalog): a map's tag data
(its bytes at data, linked to base), or the playing map's own (data NULL:
its pointers are good, its tags checked as it loaded) */
struct spawn_view
{
	byte const *data;
	unsigned long base;
	unsigned long size;
	struct spawn_tag_instance const *instances;
	long tag_count;
};

/* a schema walk over a tag (or a whole map's tags) */
struct spawn_walk
{
	short kind;
	struct spawn_source *source;
	/* the tags' memory, and the bytes of it walked (a bit a byte: a block
	is walked once) */
	byte *tags;
	unsigned long size;
	byte *visited;
	boolean failed;
	/* the tags found (a closure), in order */
	long *found;
	long found_count;
	long found_maximum;
};

enum
{
	/* moves a map's pointers to its memory */
	_walk_relocate,
	/* finds the tags a tag needs */
	_walk_closure,
	/* makes a loaded tag's references the playing map's */
	_walk_remap,
};

/* ---------- prototypes */

static void spawn_walk_fields(struct spawn_walk *walk, byte *base, struct tag_schema_definition const *definition,
	short depth);
void platform_log(char const *format, ...);
/* the game state's data of lights, effects and looping sounds
(object_lights.c's, effects.c's, game_sound.c's) */
extern struct data_array *light_data;
extern struct data_array *effect_data;
extern struct data_array *game_looping_sound_data;
/* cache_files.c's (port) */
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);

/* ---------- globals */

static struct
{
	boolean listed;
	boolean catalog_ready;
	boolean catalog_written;
	struct spawn_source sources[MAXIMUM_SPAWN_SOURCES];
	short source_count;
	/* the sources by size, smallest first */
	short order[MAXIMUM_SPAWN_SOURCES];

	struct spawn_scanned_entry *scanned;
	long scanned_count;
	long scanned_maximum;

	struct spawn_tags_entry catalog[MAXIMUM_SPAWN_ENTRIES];
	long catalog_count;

	/* the job of the moment: a scan or a decompression */
	struct spawn_inflater job;
	boolean job_active;
	/* a decompression asked for (a load waits on it) */
	short prepare_wanted;

	/* the tag table with the loaded tags, and the playing map's own (to put
	back) */
	struct spawn_tag_instance *table;
	long table_count;
	struct spawn_tag_instance *map_table;
	long map_table_count;
	/* the source of each loaded tag (NONE: none), by its place past
	SPAWN_TAGS_FIRST_INDEX */
	signed char owners[SPAWN_TAGS_LAST_INDEX - SPAWN_TAGS_FIRST_INDEX + 1];
	long next_index;

	struct spawn_tags_load loads[MAXIMUM_SPAWN_LOADS];
	long load_count;
	long generation;
} spawn_tags;

/* ---------- small things */

static unsigned long spawn_now(
	void)
{
	return system_milliseconds();
}

static boolean spawn_same_name(
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

static boolean spawn_prefix(
	char const *text,
	char const *prefix)
{
	for (; *prefix; text++, prefix++)
	{
		if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix))
			return FALSE;
	}
	return TRUE;
}

static char const *spawn_last_component(
	char const *path)
{
	char const *slash = strrchr(path, '\\');

	return slash ? slash + 1 : path;
}

static struct tag_schema_group const *spawn_schema_group(
	unsigned long group_tag)
{
	struct tag_schema_group const *const *list;

	for (list = tag_schema_group_lists; *list; list++)
	{
		struct tag_schema_group const *group;

		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group->definition ? group : NULL;
		}
	}

	return NULL;
}

/* a group whose tags are a map's own, never loaded from another */
static boolean spawn_group_is_maps_own(
	unsigned long group_tag)
{
	return group_tag == 'scnr' || group_tag == 'sbsp' || group_tag == 'matg' || group_tag == 'hudg' ||
		group_tag == 'tagc';
}

static void spawn_source_path(
	struct spawn_source const *source,
	char *path,
	unsigned long size)
{
	snprintf(path, size, "%s%s.map", cache_files_map_directory(), source->name);
}

static void spawn_prepared_path(
	struct spawn_source const *source,
	char const *extension,
	char *path,
	unsigned long size)
{
	snprintf(path, size, "%s\\%s.%s", SPAWN_TAGS_FOLDER, source->name, extension);
}

static boolean spawn_read_at(
	HANDLE file,
	unsigned long offset,
	void *buffer,
	unsigned long size)
{
	DWORD read = 0;

	if (SetFilePointer(file, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER && offset)
		return FALSE;
	return ReadFile(file, buffer, size, &read, NULL) && read == size;
}

static short spawn_source_named(
	char const *name)
{
	short index;

	for (index = 0; index < spawn_tags.source_count; index++)
	{
		if (spawn_same_name(spawn_tags.sources[index].name, name))
			return index;
	}

	return NONE;
}

/* ---------- the maps */

/* a map's header, if it is an Xbox map of this build's cache version whose
tag data fits the tag cache */
static boolean spawn_header_valid(
	struct spawn_cache_header const *header,
	unsigned long file_size)
{
	return header->header_signature == SPAWN_TAGS_HEADER_SIGNATURE &&
		header->footer_signature == SPAWN_TAGS_FOOTER_SIGNATURE &&
		header->version == SPAWN_TAGS_CACHE_VERSION &&
		header->file_length > SPAWN_CACHE_HEADER_SIZE &&
		header->tag_data_size >= (long)sizeof(struct spawn_tag_header) &&
		header->tag_data_size <= TAG_CACHE_SIZE &&
		header->tag_data_offset >= SPAWN_CACHE_HEADER_SIZE &&
		header->tag_data_offset <= header->file_length - header->tag_data_size &&
		file_size > SPAWN_CACHE_HEADER_SIZE;
}

static int spawn_source_compare_size(
	void const *a,
	void const *b)
{
	struct spawn_source const *first = &spawn_tags.sources[*(short const *)a];
	struct spawn_source const *second = &spawn_tags.sources[*(short const *)b];

	if (first->file_size != second->file_size)
		return first->file_size < second->file_size ? -1 : 1;
	return strcmp(first->name, second->name);
}

static void spawn_sources_list(
	void)
{
	char pattern[256];
	WIN32_FIND_DATAA data;
	HANDLE find;
	short index;

	spawn_tags.listed = TRUE;
	spawn_tags.prepare_wanted = NONE;
	snprintf(pattern, sizeof(pattern), "%s*.map", cache_files_map_directory());
	/* (a handle of its own, as custom_edition_maps.c's listing explains) */
	find = FindFirstFileA(pattern, &data);
	if (find == INVALID_HANDLE_VALUE)
		return;
	do
	{
		struct spawn_source *source;
		char name[sizeof(data.cFileName) + 1];
		char path[256];
		char *extension;
		HANDLE file;
		DWORD size;

		if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || spawn_tags.source_count >= MAXIMUM_SPAWN_SOURCES)
			continue;
		memcpy(name, data.cFileName, sizeof(data.cFileName));
		name[sizeof(data.cFileName)] = 0;
		extension = strrchr(name, '.');
		if (!extension || !spawn_same_name(extension, ".map"))
			continue;
		*extension = 0;
		if (!name[0] || strlen(name) >= SPAWN_TAGS_MAP_NAME_LENGTH || spawn_same_name(name, "ui") ||
			spawn_same_name(name, "bitmaps") || spawn_same_name(name, "sounds") || spawn_same_name(name, "loc"))
		{
			continue;
		}
		source = &spawn_tags.sources[spawn_tags.source_count];
		memset(source, 0, sizeof(*source));
		strcpy(source->name, name);
		source->prepared = INVALID_HANDLE_VALUE;
		spawn_source_path(source, path, sizeof(path));
		file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (file == INVALID_HANDLE_VALUE)
			continue;
		size = GetFileSize(file, NULL);
		if (size != INVALID_FILE_SIZE && spawn_read_at(file, 0, &source->header, sizeof(source->header)) &&
			spawn_header_valid(&source->header, size))
		{
			source->file_size = size;
			source->compressed = size != (DWORD)source->header.file_length;
			source->header.name[sizeof(source->header.name) - 1] = 0;
			spawn_tags.source_count++;
		}
		CloseHandle(file);
	} while (FindNextFileA(find, &data));
	CloseHandle(find);

	for (index = 0; index < spawn_tags.source_count; index++)
		spawn_tags.order[index] = index;
	qsort(spawn_tags.order, (size_t)spawn_tags.source_count, sizeof(spawn_tags.order[0]), spawn_source_compare_size);
	platform_log("spawn menu: %d maps to scan", spawn_tags.source_count);
}

/* ---------- inflating */

static void spawn_job_end(
	void)
{
	struct spawn_inflater *job = &spawn_tags.job;

	if (job->stream_open)
		inflateEnd(&job->stream);
	if (job->input != INVALID_HANDLE_VALUE && job->input)
		CloseHandle(job->input);
	if (job->output != INVALID_HANDLE_VALUE && job->output)
		CloseHandle(job->output);
	if (job->input_buffer)
		free(job->input_buffer);
	if (job->output_buffer)
		free(job->output_buffer);
	if (job->capture)
		free(job->capture);
	memset(job, 0, sizeof(*job));
	job->input = INVALID_HANDLE_VALUE;
	job->output = INVALID_HANDLE_VALUE;
	spawn_tags.job_active = FALSE;
}

static boolean spawn_job_begin(
	short source_index,
	boolean scanning)
{
	struct spawn_inflater *job = &spawn_tags.job;
	struct spawn_source *source = &spawn_tags.sources[source_index];
	char path[256];

	memset(job, 0, sizeof(*job));
	job->input = INVALID_HANDLE_VALUE;
	job->output = INVALID_HANDLE_VALUE;
	job->source = source_index;
	job->scanning = scanning;
	spawn_tags.job_active = TRUE;
	spawn_source_path(source, path, sizeof(path));
	job->input = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	job->input_buffer = malloc(SPAWN_INFLATE_CHUNK);
	job->output_buffer = malloc(SPAWN_INFLATE_CHUNK);
	if (job->input == INVALID_HANDLE_VALUE || !job->input_buffer || !job->output_buffer)
		return FALSE;
	if (scanning)
	{
		job->capture_offset = (unsigned long)source->header.tag_data_offset;
		job->capture_size = (unsigned long)source->header.tag_data_size;
		job->capture = malloc(job->capture_size);
		job->end = job->capture_offset + job->capture_size;
		if (!job->capture)
			return FALSE;
	}
	else
	{
		DWORD written = 0;

		CreateDirectoryA(SPAWN_TAGS_FOLDER, NULL);
		spawn_prepared_path(source, "tmp", path, sizeof(path));
		job->output = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
		job->end = (unsigned long)source->header.file_length;
		if (job->output == INVALID_HANDLE_VALUE ||
			!WriteFile(job->output, &source->header, sizeof(source->header), &written, NULL) ||
			written != sizeof(source->header))
		{
			return FALSE;
		}
	}
	job->position = SPAWN_CACHE_HEADER_SIZE;
	if (!source->compressed)
	{
		/* (a map not compressed: its tags read at once) */
		return scanning ? spawn_read_at(job->input, job->capture_offset, job->capture, job->capture_size) : FALSE;
	}
	if (SetFilePointer(job->input, SPAWN_CACHE_HEADER_SIZE, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		return FALSE;
	if (inflateInit(&job->stream) != Z_OK)
		return FALSE;
	job->stream_open = TRUE;

	return TRUE;
}

/* 1 while there is more, 0 once it has all it wanted, -1 if it failed */
static short spawn_job_step(
	void)
{
	struct spawn_inflater *job = &spawn_tags.job;
	unsigned long produced;
	int result;

	if (!spawn_tags.sources[job->source].compressed)
		return 0;
	if (!job->stream.avail_in)
	{
		DWORD read = 0;

		if (!ReadFile(job->input, job->input_buffer, SPAWN_INFLATE_CHUNK, &read, NULL) || !read)
			return -1;
		job->stream.next_in = job->input_buffer;
		job->stream.avail_in = read;
	}
	job->stream.next_out = job->output_buffer;
	job->stream.avail_out = SPAWN_INFLATE_CHUNK;
	result = inflate(&job->stream, Z_NO_FLUSH);
	if (result != Z_OK && result != Z_STREAM_END)
		return -1;
	produced = SPAWN_INFLATE_CHUNK - job->stream.avail_out;
	if (produced > job->end - job->position)
		produced = job->end - job->position;
	if (job->output != INVALID_HANDLE_VALUE && produced)
	{
		DWORD written = 0;

		if (!WriteFile(job->output, job->output_buffer, produced, &written, NULL) || written != produced)
			return -1;
	}
	if (job->capture && job->position + produced > job->capture_offset)
	{
		unsigned long start = job->position > job->capture_offset ? job->position - job->capture_offset : 0;
		unsigned long skip = job->position < job->capture_offset ? job->capture_offset - job->position : 0;

		if (skip < produced)
			memcpy(job->capture + start, job->output_buffer + skip, produced - skip);
	}
	job->position += produced;
	if (job->position >= job->end)
		return 0;
	return result == Z_STREAM_END ? -1 : 1;
}

/* ---------- reading a map's things */

static void const *spawn_view_get(
	struct spawn_view const *view,
	void const *address,
	unsigned long size)
{
	unsigned long value = (unsigned long)address;
	unsigned long offset;

	if (!value)
		return NULL;
	if (!view->data)
		return address;
	offset = value - view->base;
	if (value < view->base || offset > view->size || size > view->size - offset)
		return NULL;
	return view->data + offset;
}

static void const *spawn_view_block(
	struct spawn_view const *view,
	struct tag_block const *block,
	unsigned long element_size)
{
	if (block->count <= 0 || (unsigned long)block->count > 0x10000)
		return NULL;
	return spawn_view_get(view, block->address, (unsigned long)block->count * element_size);
}

static struct spawn_tag_instance const *spawn_view_instance(
	struct spawn_view const *view,
	long tag_index)
{
	short absolute_index = (short)tag_index;

	if (tag_index == NONE || absolute_index < 0 || absolute_index >= view->tag_count ||
		view->instances[absolute_index].tag_index != tag_index)
	{
		return NULL;
	}
	return &view->instances[absolute_index];
}

static void const *spawn_view_tag(
	struct spawn_view const *view,
	long tag_index,
	unsigned long group_tag,
	unsigned long size)
{
	struct spawn_tag_instance const *instance = spawn_view_instance(view, tag_index);

	if (!instance || (instance->group_tag != group_tag && instance->parent_group_tags[0] != group_tag &&
		instance->parent_group_tags[1] != group_tag))
	{
		return NULL;
	}
	return spawn_view_get(view, instance->base_address, size);
}

/* the tag's name, if it is a terminated string in the tags */
static char const *spawn_view_name(
	struct spawn_view const *view,
	struct spawn_tag_instance const *instance)
{
	unsigned long value = (unsigned long)instance->name;
	unsigned long offset;

	if (!view->data)
		return instance->name ? instance->name : "";
	offset = value - view->base;
	if (!value || value < view->base || offset >= view->size ||
		!memchr(view->data + offset, 0, MIN(view->size - offset, SPAWN_TAGS_PATH_LENGTH)))
	{
		return NULL;
	}
	return (char const *)view->data + offset;
}

/* a weapon's pickup icon: its HUD's messaging icon sequence, else NONE */
static short spawn_view_weapon_icon(
	struct spawn_view const *view,
	struct weapon_definition const *weapon)
{
	struct weapon_hud_interface_definition const *hud = spawn_view_tag(view,
		weapon->weapon.interface_definition.hud_interface.index, 'wphi', sizeof(*hud));

	return hud ? hud->messaging_icon.sequence_index : NONE;
}

static boolean spawn_weapon_carried(
	struct spawn_view const *view,
	struct weapon_definition const *weapon,
	char const *path)
{
	return !spawn_prefix(path, "vehicles\\") && !spawn_prefix(path, "characters\\") &&
		spawn_view_tag(view, weapon->weapon.interface_definition.hud_interface.index, 'wphi', 1) &&
		spawn_view_instance(view, weapon->weapon.interface_definition.first_person_model.index);
}

static boolean spawn_vehicle_driven(
	struct spawn_view const *view,
	struct unit_definition const *vehicle,
	char const *path)
{
	struct unit_seat const *seats = spawn_view_block(view, &vehicle->unit.seats, sizeof(struct unit_seat));
	long index;

	if (!seats || spawn_prefix(path, "cinematics\\") || spawn_prefix(path, "scenery\\") ||
		spawn_prefix(path, "levels\\") || strstr(path, "cinematic") || strstr(path, "playback") ||
		strstr(path, "crashing") || strstr(path, "astral") || strstr(path, "lifepod") ||
		strstr(path, "fighterbomber"))
	{
		return FALSE;
	}
	for (index = 0; index < vehicle->unit.seats.count; index++)
	{
		if (TEST_FLAG(seats[index].flags, _unit_seat_driver_bit) &&
			!TEST_FLAG(seats[index].flags, SPAWN_SEAT_LOCKED_BIT))
		{
			return TRUE;
		}
	}

	return FALSE;
}

/* a character's faction: its unit's team, else what its path says */
static short spawn_character_faction(
	struct spawn_view const *view,
	struct actor_variant_definition const *variant,
	char const *path)
{
	struct unit_definition const *unit = spawn_view_tag(view, variant->unit_reference.index, 'unit', sizeof(*unit));

	switch (unit ? unit->unit.default_team : _game_team_default)
	{
	case _game_team_player:
	case _game_team_human:
		return _spawn_faction_unsc;
	case _game_team_covenant:
		return _spawn_faction_covenant;
	case _game_team_flood:
		return _spawn_faction_flood;
	case _game_team_sentinel:
		return _spawn_faction_forerunner;
	}
	if (strstr(path, "flood"))
		return _spawn_faction_flood;
	if (strstr(path, "sentinel") || strstr(path, "monitor"))
		return _spawn_faction_forerunner;
	if (strstr(path, "elite") || strstr(path, "grunt") || strstr(path, "jackal") || strstr(path, "hunter") ||
		strstr(path, "engineer"))
	{
		return _spawn_faction_covenant;
	}

	return _spawn_faction_unsc;
}

static boolean spawn_character_placed(
	struct spawn_view const *view,
	struct actor_variant_definition const *variant,
	char const *path)
{
	struct actor_definition const *actor = spawn_view_tag(view, variant->actor_reference.index, ACTOR_DEFINITION_TAG,
		sizeof(*actor));

	return actor && !spawn_prefix(path, "vehicles\\") &&
		spawn_view_tag(view, variant->unit_reference.index, 'unit', 1) &&
		!TEST_FLAG(actor->flags, _actor_definition_swarm_actor_bit);
}

/* the things of the tags in view, to entries (count at most): returns how
many */
static long spawn_view_entries(
	struct spawn_view const *view,
	struct spawn_tags_entry *entries,
	long count)
{
	/* the ammunition each weapon takes (its magazines' objects), for its
	pickups' icons */
	long ammunition[64];
	short ammunition_icons[64];
	short ammunition_count = 0;
	long found = 0;
	long tag;

	for (tag = 0; tag < view->tag_count; tag++)
	{
		struct spawn_tag_instance const *instance = &view->instances[tag];
		struct weapon_definition const *weapon;
		struct weapon_magazine_definition const *magazines;
		long magazine;

		if (instance->group_tag != WEAPON_DEFINITION_TAG ||
			!(weapon = spawn_view_get(view, instance->base_address, sizeof(*weapon))))
		{
			continue;
		}
		magazines = spawn_view_block(view, &weapon->weapon.magazines, sizeof(*magazines));
		for (magazine = 0; magazines && magazine < weapon->weapon.magazines.count; magazine++)
		{
			struct weapon_ammunition_object const *objects = spawn_view_block(view,
				&magazines[magazine].ammunition_objects, sizeof(*objects));
			long object;

			for (object = 0; objects && object < magazines[magazine].ammunition_objects.count; object++)
			{
				if (ammunition_count < NUMBEROF(ammunition))
				{
					ammunition[ammunition_count] = objects[object].object.index;
					ammunition_icons[ammunition_count++] = spawn_view_weapon_icon(view, weapon);
				}
			}
		}
	}

	for (tag = 0; tag < view->tag_count && found < count; tag++)
	{
		struct spawn_tag_instance const *instance = &view->instances[tag];
		struct spawn_tags_entry *entry = &entries[found];
		char const *path = spawn_view_name(view, instance);
		boolean add = FALSE;

		if (!path || !path[0] || strlen(path) >= SPAWN_TAGS_PATH_LENGTH)
			continue;
		memset(entry, 0, sizeof(*entry));
		entry->group_tag = instance->group_tag;
		entry->icon_kind = _spawn_icon_none;
		entry->icon_value = NONE;
		switch (instance->group_tag)
		{
		case WEAPON_DEFINITION_TAG:
		{
			struct weapon_definition const *weapon = spawn_view_get(view, instance->base_address, sizeof(*weapon));

			if (weapon && spawn_weapon_carried(view, weapon, path))
			{
				add = TRUE;
				entry->tab = _spawn_tab_weapons;
				entry->icon_value = spawn_view_weapon_icon(view, weapon);
				entry->icon_kind = entry->icon_value != NONE ? _spawn_icon_message : _spawn_icon_none;
			}
			break;
		}
		case EQUIPMENT_DEFINITION_TAG:
		{
			struct equipment_definition const *equipment = spawn_view_get(view, instance->base_address,
				sizeof(*equipment));
			short index;

			if (!equipment)
				break;
			add = TRUE;
			switch (equipment->equipment.powerup_type)
			{
			case _equipment_powerup_grenade:
				entry->tab = _spawn_tab_ammunition;
				entry->icon_kind = _spawn_icon_grenade;
				entry->icon_value = equipment->equipment.grenade_type;
				break;
			case _equipment_powerup_none:
				entry->tab = _spawn_tab_ammunition;
				for (index = 0; index < ammunition_count; index++)
				{
					if (ammunition[index] == instance->tag_index && ammunition_icons[index] != NONE)
					{
						entry->icon_kind = _spawn_icon_message;
						entry->icon_value = ammunition_icons[index];
						break;
					}
				}
				break;
			default:
				entry->tab = _spawn_tab_equipment;
				break;
			}
			break;
		}
		case ACTOR_VARIANT_DEFINITION_TAG:
		{
			struct actor_variant_definition const *variant = spawn_view_get(view, instance->base_address,
				sizeof(*variant));

			if (variant && spawn_character_placed(view, variant, path))
			{
				add = TRUE;
				entry->tab = _spawn_tab_characters;
				entry->faction = spawn_character_faction(view, variant, path);
			}
			break;
		}
		case VEHICLE_DEFINITION_TAG:
		{
			struct unit_definition const *vehicle = spawn_view_get(view, instance->base_address, sizeof(*vehicle));

			if (vehicle && spawn_vehicle_driven(view, vehicle, path))
			{
				add = TRUE;
				entry->tab = _spawn_tab_vehicles;
			}
			break;
		}
		}
		if (add)
		{
			strcpy(entry->path, path);
			found++;
		}
	}

	return found;
}

/* the view of a map's tag data, if its header and table are in it */
static boolean spawn_view_of_tags(
	struct spawn_view *view,
	byte const *data,
	unsigned long base,
	unsigned long size)
{
	struct spawn_tag_header const *header = (struct spawn_tag_header const *)data;

	view->data = data;
	view->base = base;
	view->size = size;
	if (size < sizeof(*header) || header->signature != SPAWN_TAGS_TAG_HEADER_SIGNATURE ||
		header->tag_count <= 0 || header->tag_count > UNSIGNED_SHORT_MAX)
	{
		return FALSE;
	}
	view->tag_count = header->tag_count;
	view->instances = spawn_view_get(view, header->instances,
		(unsigned long)header->tag_count * sizeof(struct spawn_tag_instance));

	return view->instances != NULL;
}

static void spawn_scanned_add(
	short source,
	struct spawn_tags_entry const *entry)
{
	if (spawn_tags.scanned_count >= spawn_tags.scanned_maximum)
	{
		long maximum = spawn_tags.scanned_maximum ? 2 * spawn_tags.scanned_maximum : 256;
		struct spawn_scanned_entry *grown = realloc(spawn_tags.scanned, (size_t)maximum * sizeof(*grown));

		if (!grown)
			return;
		spawn_tags.scanned = grown;
		spawn_tags.scanned_maximum = maximum;
	}
	spawn_tags.scanned[spawn_tags.scanned_count].source = source;
	spawn_tags.scanned[spawn_tags.scanned_count].entry = *entry;
	spawn_tags.scanned_count++;
}

static void spawn_scan_finished(
	short source_index)
{
	struct spawn_source *source = &spawn_tags.sources[source_index];
	struct spawn_inflater *job = &spawn_tags.job;
	struct spawn_view view;
	static struct spawn_tags_entry entries[512];
	long count = 0;
	long index;

	if (spawn_view_of_tags(&view, job->capture, SPAWN_TAGS_SOURCE_BASE, job->capture_size))
		count = spawn_view_entries(&view, entries, NUMBEROF(entries));
	for (index = 0; index < count; index++)
	{
		strcpy(entries[index].source, source->name);
		entries[index].source_checksum = source->header.checksum;
		spawn_scanned_add(source_index, &entries[index]);
	}
	source->scan_state = _source_scanned;
}

/* ---------- the catalog */

static int spawn_entry_compare(
	void const *a,
	void const *b)
{
	struct spawn_tags_entry const *first = a;
	struct spawn_tags_entry const *second = b;
	char const *x = spawn_last_component(first->path);
	char const *y = spawn_last_component(second->path);

	if (first->tab != second->tab)
		return first->tab - second->tab;
	while (*x && tolower((unsigned char)*x) == tolower((unsigned char)*y))
	{
		x++;
		y++;
	}
	if (tolower((unsigned char)*x) != tolower((unsigned char)*y))
		return tolower((unsigned char)*x) - tolower((unsigned char)*y);
	return strcmp(first->path, second->path);
}

static void spawn_catalog_build(
	void)
{
	short order;

	spawn_tags.catalog_count = 0;
	for (order = 0; order < spawn_tags.source_count; order++)
	{
		short source = spawn_tags.order[order];
		long index;

		for (index = 0; index < spawn_tags.scanned_count; index++)
		{
			struct spawn_tags_entry const *entry = &spawn_tags.scanned[index].entry;
			long existing;

			if (spawn_tags.scanned[index].source != source)
				continue;
			for (existing = 0; existing < spawn_tags.catalog_count; existing++)
			{
				if (spawn_tags.catalog[existing].group_tag == entry->group_tag &&
					spawn_same_name(spawn_tags.catalog[existing].path, entry->path))
				{
					break;
				}
			}
			if (existing < spawn_tags.catalog_count)
			{
				/* (an icon the first map's had not) */
				if (spawn_tags.catalog[existing].icon_kind == _spawn_icon_none &&
					entry->icon_kind != _spawn_icon_none)
				{
					spawn_tags.catalog[existing].icon_kind = entry->icon_kind;
					spawn_tags.catalog[existing].icon_value = entry->icon_value;
				}
			}
			else if (spawn_tags.catalog_count < MAXIMUM_SPAWN_ENTRIES)
			{
				spawn_tags.catalog[spawn_tags.catalog_count++] = *entry;
			}
		}
	}
	qsort(spawn_tags.catalog, (size_t)spawn_tags.catalog_count, sizeof(spawn_tags.catalog[0]), spawn_entry_compare);
}

static void spawn_catalog_write(
	void)
{
	FILE *file;
	short source;

	CreateDirectoryA(SPAWN_TAGS_FOLDER, NULL);
	file = fopen(SPAWN_TAGS_CATALOG, "w");
	if (!file)
		return;
	fprintf(file, "%s\n", SPAWN_TAGS_CATALOG_SIGNATURE);
	for (source = 0; source < spawn_tags.source_count; source++)
	{
		long index;

		if (spawn_tags.sources[source].scan_state != _source_scanned)
			continue;
		fprintf(file, "map %s %lu %08lx\n", spawn_tags.sources[source].name, spawn_tags.sources[source].file_size,
			spawn_tags.sources[source].header.checksum);
		for (index = 0; index < spawn_tags.scanned_count; index++)
		{
			struct spawn_tags_entry const *entry = &spawn_tags.scanned[index].entry;

			if (spawn_tags.scanned[index].source == source)
			{
				fprintf(file, "entry %08lx %d %d %d %d %s\n", entry->group_tag, entry->tab, entry->faction,
					entry->icon_kind, entry->icon_value, entry->path);
			}
		}
	}
	fclose(file);
}

/* what the catalog file says of the maps that are as they were */
static void spawn_catalog_read(
	void)
{
	char line[SPAWN_TAGS_PATH_LENGTH + 64];
	FILE *file = fopen(SPAWN_TAGS_CATALOG, "r");
	short source = NONE;

	if (!file)
		return;
	if (!fgets(line, sizeof(line), file) || strncmp(line, SPAWN_TAGS_CATALOG_SIGNATURE, strlen(SPAWN_TAGS_CATALOG_SIGNATURE)))
	{
		fclose(file);
		return;
	}
	while (fgets(line, sizeof(line), file))
	{
		char name[SPAWN_TAGS_MAP_NAME_LENGTH];
		unsigned long size, checksum, group_tag;
		int tab, faction, icon_kind, icon_value, used = 0;

		line[strcspn(line, "\r\n")] = 0;
		if (sscanf(line, "map %31s %lu %lx", name, &size, &checksum) == 3)
		{
			source = spawn_source_named(name);
			if (source != NONE && (spawn_tags.sources[source].file_size != size ||
				spawn_tags.sources[source].header.checksum != checksum ||
				spawn_tags.sources[source].scan_state != _source_unscanned))
			{
				source = NONE;
			}
			if (source != NONE)
				spawn_tags.sources[source].scan_state = _source_scanned;
		}
		else if (source != NONE && sscanf(line, "entry %lx %d %d %d %d %n", &group_tag, &tab, &faction, &icon_kind,
			&icon_value, &used) == 5 && used > 0 && line[used] && strlen(line + used) < SPAWN_TAGS_PATH_LENGTH &&
			tab >= 0 && tab < NUMBER_OF_SPAWN_TABS && faction >= 0 && faction < NUMBER_OF_SPAWN_FACTIONS)
		{
			struct spawn_tags_entry entry;

			memset(&entry, 0, sizeof(entry));
			entry.group_tag = group_tag;
			entry.tab = (short)tab;
			entry.faction = (short)faction;
			entry.icon_kind = (short)icon_kind;
			entry.icon_value = (short)icon_value;
			strcpy(entry.path, line + used);
			strcpy(entry.source, spawn_tags.sources[source].name);
			entry.source_checksum = spawn_tags.sources[source].header.checksum;
			spawn_scanned_add(source, &entry);
		}
	}
	fclose(file);
}

boolean spawn_tags_catalog_ready(
	real *progress)
{
	short scanned = 0;
	short source;

	for (source = 0; source < spawn_tags.source_count; source++)
	{
		if (spawn_tags.sources[source].scan_state == _source_scanned ||
			spawn_tags.sources[source].scan_state == _source_failed)
		{
			scanned++;
		}
	}
	if (progress)
		*progress = spawn_tags.source_count ? (real)scanned / spawn_tags.source_count : 1.0f;

	return spawn_tags.listed && spawn_tags.catalog_ready;
}

long spawn_tags_catalog_count(
	void)
{
	return spawn_tags.catalog_count;
}

struct spawn_tags_entry const *spawn_tags_catalog_get(
	long index)
{
	return index >= 0 && index < spawn_tags.catalog_count ? &spawn_tags.catalog[index] : NULL;
}

long spawn_tags_playing_map_entries(
	struct spawn_tags_entry *entries,
	long count)
{
	struct spawn_view view;

	memset(&view, 0, sizeof(view));
	view.instances = cache_files_tag_instances(&view.tag_count);
	if (!view.instances)
		return 0;
	return spawn_view_entries(&view, entries, count);
}

/* ---------- the frame's work */

static void spawn_prepare_finished(
	short source_index,
	boolean succeeded)
{
	struct spawn_source *source = &spawn_tags.sources[source_index];
	char temporary[256];
	char path[256];

	spawn_prepared_path(source, "tmp", temporary, sizeof(temporary));
	spawn_prepared_path(source, "map", path, sizeof(path));
	if (succeeded)
	{
		DeleteFileA(path);
		succeeded = MoveFileA(temporary, path);
	}
	else
	{
		DeleteFileA(temporary);
	}
	source->prepare_state = succeeded ? _prepare_none : _prepare_failed;
	platform_log("spawn menu: %s %s", source->name, succeeded ? "decompressed" : "could not be decompressed");
}

/* whether the map's decompressed copy (or the map) is open for reads, and
if not, whether it can be (is there, of the map's length and header) */
static boolean spawn_source_open_prepared(
	struct spawn_source *source)
{
	char path[256];
	struct spawn_cache_header header;
	HANDLE file;

	if (source->prepared != INVALID_HANDLE_VALUE)
		return TRUE;
	if (source->compressed)
		spawn_prepared_path(source, "map", path, sizeof(path));
	else
		spawn_source_path(source, path, sizeof(path));
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return FALSE;
	if (GetFileSize(file, NULL) != (DWORD)source->header.file_length ||
		!spawn_read_at(file, 0, &header, sizeof(header)) || memcmp(&header, &source->header, sizeof(header)))
	{
		CloseHandle(file);
		return FALSE;
	}
	source->prepared = file;
	source->prepare_state = _prepare_ready;

	return TRUE;
}

void spawn_tags_update(
	boolean hurry)
{
	unsigned long start = spawn_now();
	unsigned long budget = hurry ? SPAWN_HURRIED_BUDGET : SPAWN_IDLE_BUDGET;

	if (!spawn_tags.listed)
	{
		spawn_sources_list();
		spawn_catalog_read();
	}
	while (spawn_now() - start < budget)
	{
		if (!spawn_tags.job_active)
		{
			short order;
			short next = NONE;

			/* (a load waiting comes first) */
			if (spawn_tags.prepare_wanted != NONE)
			{
				struct spawn_source *source = &spawn_tags.sources[spawn_tags.prepare_wanted];

				if (!spawn_source_open_prepared(source) && source->prepare_state != _prepare_failed)
				{
					source->prepare_state = _prepare_inflating;
					if (!spawn_job_begin(spawn_tags.prepare_wanted, FALSE))
					{
						spawn_job_end();
						spawn_prepare_finished(spawn_tags.prepare_wanted, FALSE);
					}
					spawn_tags.prepare_wanted = NONE;
					continue;
				}
				spawn_tags.prepare_wanted = NONE;
			}
			for (order = 0; order < spawn_tags.source_count && next == NONE; order++)
			{
				if (spawn_tags.sources[spawn_tags.order[order]].scan_state == _source_unscanned)
					next = spawn_tags.order[order];
			}
			if (next == NONE)
			{
				if (!spawn_tags.catalog_ready)
				{
					spawn_catalog_build();
					spawn_tags.catalog_ready = TRUE;
					if (!spawn_tags.catalog_written)
					{
						spawn_catalog_write();
						spawn_tags.catalog_written = TRUE;
					}
					platform_log("spawn menu: %ld things in %d maps", spawn_tags.catalog_count, spawn_tags.source_count);
				}
				break;
			}
			spawn_tags.sources[next].scan_state = _source_scanning;
			spawn_tags.catalog_written = FALSE;
			if (!spawn_job_begin(next, TRUE))
			{
				spawn_job_end();
				spawn_tags.sources[next].scan_state = _source_failed;
				platform_log("spawn menu: %s could not be read", spawn_tags.sources[next].name);
			}
			continue;
		}
		{
			short source = spawn_tags.job.source;
			boolean scanning = spawn_tags.job.scanning;
			short result = spawn_job_step();

			if (result > 0)
				continue;
			if (scanning)
			{
				if (result == 0)
				{
					spawn_scan_finished(source);
				}
				else
				{
					spawn_tags.sources[source].scan_state = _source_failed;
					platform_log("spawn menu: %s could not be read", spawn_tags.sources[source].name);
				}
				spawn_job_end();
			}
			else
			{
				spawn_job_end();
				spawn_prepare_finished(source, result == 0);
			}
		}
	}
}

/* ---------- walking tags by their schemas */

/* moves a pointer of a map's tags (linked to SPAWN_TAGS_SOURCE_BASE) to
their memory, if it and the bytes it points at are in the tags; one moved
already stays. FALSE if it is neither */
static boolean spawn_relocate_pointer(
	struct spawn_walk *walk,
	void **pointer,
	unsigned long size)
{
	unsigned long value = (unsigned long)*pointer;
	unsigned long offset;

	if (!value)
		return TRUE;
	if (value >= (unsigned long)walk->tags && value - (unsigned long)walk->tags < walk->size)
		return TRUE;
	offset = value - SPAWN_TAGS_SOURCE_BASE;
	if (value < SPAWN_TAGS_SOURCE_BASE || offset >= walk->size || size > walk->size - offset)
		return FALSE;
	*pointer = walk->tags + offset;

	return TRUE;
}

static boolean spawn_walk_visit(
	struct spawn_walk *walk,
	void const *address)
{
	unsigned long offset = (unsigned long)address - (unsigned long)walk->tags;

	if (!walk->visited || offset >= walk->size)
		return TRUE;
	if (walk->visited[offset >> 3] & (1 << (offset & 7)))
		return FALSE;
	walk->visited[offset >> 3] |= (byte)(1 << (offset & 7));

	return TRUE;
}

/* a source tag index a loaded tag's reference or tag index names: its
place in the map's table, or NONE */
static long spawn_source_absolute_index(
	struct spawn_source const *source,
	long tag_index)
{
	struct spawn_tag_header const *header = (struct spawn_tag_header const *)source->tags;
	short absolute_index = (short)tag_index;

	if (tag_index == NONE || absolute_index < 0 || absolute_index >= source->tag_count ||
		header->instances[absolute_index].tag_index != tag_index)
	{
		return NONE;
	}
	return absolute_index;
}

static void spawn_closure_add(
	struct spawn_walk *walk,
	long absolute_index);

static void spawn_walk_tag_index(
	struct spawn_walk *walk,
	long *tag_index,
	unsigned long *group_tag)
{
	long absolute_index = spawn_source_absolute_index(walk->source, *tag_index);

	if (walk->kind == _walk_closure)
	{
		if (absolute_index != NONE)
			spawn_closure_add(walk, absolute_index);
	}
	else if (walk->kind == _walk_remap && *tag_index != NONE)
	{
		long global_index = absolute_index != NONE ? walk->source->global_indices[absolute_index] : NONE;

		if (global_index < 0)
			global_index = NONE;
		*tag_index = global_index;
		if (group_tag && global_index != NONE)
		{
			long count;
			struct spawn_tag_instance const *instances = cache_files_tag_instances(&count);

			if (instances && (short)global_index < count)
				*group_tag = instances[(short)global_index].group_tag;
		}
	}
}

static void spawn_walk_fields(
	struct spawn_walk *walk,
	byte *base,
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth >= MAXIMUM_SPAWN_WALK_DEPTH)
	{
		walk->failed = TRUE;
		return;
	}
	/* (a model part's buffers, which are not fields: the validator finds
	them in the header's, or the part is drawn with none) */
	if (walk->kind == _walk_relocate && tag_schema_is_model_geometry_part(definition))
	{
		void **pointers[TAG_SCHEMA_MODEL_GEOMETRY_PART_POINTERS];
		short count = tag_schema_model_geometry_part_pointers(base, pointers);
		short index;

		for (index = 0; index < count; index++)
			spawn_relocate_pointer(walk, pointers[index], 1);
	}
	for (field = definition->fields; field->type != _tag_schema_terminator && !walk->failed; field++)
	{
		short index;

		for (index = 0; index < field->count && !walk->failed; index++)
		{
			byte *address = base + field->offset + index * field->size;

			switch (field->type)
			{
			case _tag_schema_struct:
				spawn_walk_fields(walk, address, field->definition, (short)(depth + 1));
				break;

			case _tag_schema_block:
			{
				struct tag_block *block = (struct tag_block *)address;
				struct tag_schema_definition const *element = field->definition;
				long element_index;

				if (block->count <= 0 || !block->address)
					break;
				if ((unsigned long)block->count > walk->size / (unsigned long)element->size)
				{
					/* (the validator refuses it) */
					if (walk->kind == _walk_relocate)
						break;
					walk->failed = TRUE;
					break;
				}
				if (walk->kind == _walk_relocate &&
					!spawn_relocate_pointer(walk, &block->address, (unsigned long)block->count * element->size))
				{
					break;
				}
				if (walk->kind == _walk_relocate && !spawn_walk_visit(walk, block->address))
					break;
				for (element_index = 0; element_index < block->count && !walk->failed; element_index++)
				{
					spawn_walk_fields(walk, (byte *)block->address + element_index * element->size, element,
						(short)(depth + 1));
				}
				break;
			}

			case _tag_schema_data:
			case _tag_schema_file_data:
				if (walk->kind == _walk_relocate)
				{
					struct tag_data *data = (struct tag_data *)address;

					if (data->size > 0)
						spawn_relocate_pointer(walk, &data->address, (unsigned long)data->size);
				}
				break;

			case _tag_schema_reference:
			{
				struct tag_reference *reference = (struct tag_reference *)address;

				if (walk->kind == _walk_relocate)
					spawn_relocate_pointer(walk, (void **)&reference->name, 1);
				else
					spawn_walk_tag_index(walk, &reference->index, &reference->group_tag);
				break;
			}

			case _tag_schema_tag_index:
				if (walk->kind != _walk_relocate)
					spawn_walk_tag_index(walk, (long *)address, NULL);
				break;
			}
		}
	}
}

/* ---------- loading a map's tags */

/* the map's tags moved to their memory (spawn_tags.h): FALSE if a pointer
the validator trusts could not be */
static boolean spawn_relocate_tags(
	struct spawn_source *source)
{
	struct spawn_walk walk;
	struct spawn_tag_header *header = (struct spawn_tag_header *)source->tags;
	unsigned long size = (unsigned long)source->header.tag_data_size;
	long index;
	boolean result = TRUE;

	memset(&walk, 0, sizeof(walk));
	walk.kind = _walk_relocate;
	walk.source = source;
	walk.tags = source->tags;
	walk.size = size;
	/* (the game's malloc and free, cseries.h: it has no calloc) */
	walk.visited = malloc(size / 8 + 1);
	if (!walk.visited)
		return FALSE;
	memset(walk.visited, 0, size / 8 + 1);
	if (header->signature != SPAWN_TAGS_TAG_HEADER_SIGNATURE || header->tag_count <= 0 ||
		header->tag_count > UNSIGNED_SHORT_MAX ||
		!spawn_relocate_pointer(&walk, (void **)&header->instances,
			(unsigned long)header->tag_count * sizeof(struct spawn_tag_instance)) ||
		header->vertex_buffer_count < 0 || header->vertex_buffer_count > (long)(size / 12) ||
		header->index_buffer_count < 0 || header->index_buffer_count > (long)(size / 12) ||
		(header->vertex_buffer_count &&
			!spawn_relocate_pointer(&walk, (void **)&header->vertex_buffers, (unsigned long)header->vertex_buffer_count * 12)) ||
		(header->index_buffer_count &&
			!spawn_relocate_pointer(&walk, (void **)&header->index_buffers, (unsigned long)header->index_buffer_count * 12)))
	{
		free(walk.visited);
		return FALSE;
	}
	/* (each buffer's Data, its bytes' address before it is registered) */
	for (index = 0; index < header->vertex_buffer_count + header->index_buffer_count && result; index++)
	{
		byte *buffer = index < header->vertex_buffer_count ?
			header->vertex_buffers + index * 12 :
			header->index_buffers + (index - header->vertex_buffer_count) * 12;

		result = spawn_relocate_pointer(&walk, (void **)(buffer + 4), 1);
	}
	for (index = 0; index < header->tag_count && result; index++)
	{
		struct spawn_tag_instance *instance = &header->instances[index];
		struct tag_schema_group const *group = spawn_schema_group(instance->group_tag);

		spawn_relocate_pointer(&walk, (void **)&instance->name, 1);
		if (instance->group_tag == 'sbsp')
			continue;
		if (!spawn_relocate_pointer(&walk, &instance->base_address, group ? (unsigned long)group->definition->size : 1))
		{
			result = FALSE;
			break;
		}
		if (group && spawn_walk_visit(&walk, instance->base_address))
			spawn_walk_fields(&walk, instance->base_address, group->definition, 0);
	}
	free(walk.visited);

	return result && !walk.failed;
}

/* the map's tags in memory of their own, moved there and checked, their
buffers registered */
static boolean spawn_source_load_tags(
	struct spawn_source *source,
	char *message,
	short message_size)
{
	unsigned long size = (unsigned long)source->header.tag_data_size;
	long index;

	if (source->tags)
		return TRUE;
	if (source->tags_failed)
	{
		snprintf(message, message_size, "%s's tags could not be loaded", source->name);
		return FALSE;
	}
	source->tags_failed = TRUE;
	source->tags = XPhysicalAlloc(size, (ULONG_PTR)-1, 0, PAGE_READWRITE);
	if (!source->tags)
	{
		snprintf(message, message_size, "no memory for %s's tags", source->name);
		return FALSE;
	}
	/* (the tags' memory must not be where they are linked to: a pointer in
	either is told apart by where it is) */
	if ((unsigned long)source->tags < SPAWN_TAGS_SOURCE_BASE + size &&
		(unsigned long)source->tags + size > SPAWN_TAGS_SOURCE_BASE)
	{
		XPhysicalFree(source->tags);
		source->tags = NULL;
		snprintf(message, message_size, "%s's tags cannot be placed", source->name);
		return FALSE;
	}
	if (!spawn_read_at(source->prepared, (unsigned long)source->header.tag_data_offset, source->tags, size) ||
		!spawn_relocate_tags(source) ||
		!tag_validate_tags_aside(source->tags, (long)size, source->header.file_length, source->name))
	{
		XPhysicalFree(source->tags);
		source->tags = NULL;
		snprintf(message, message_size, "%s's tags are damaged", source->name);
		platform_log("spawn menu: %s's tags are damaged", source->name);
		return FALSE;
	}
	source->tag_count = ((struct spawn_tag_header *)source->tags)->tag_count;
	source->global_indices = malloc((size_t)source->tag_count * sizeof(long));
	if (!source->global_indices)
	{
		XPhysicalFree(source->tags);
		source->tags = NULL;
		snprintf(message, message_size, "no memory for %s's tags", source->name);
		return FALSE;
	}
	for (index = 0; index < source->tag_count; index++)
		source->global_indices[index] = SPAWN_TAG_UNKNOWN;
	tags_header_register_vertex_and_index_buffers((struct cache_file_tag_header *)source->tags);
	source->tags_failed = FALSE;
	platform_log("spawn menu: %s's %ld tags loaded", source->name, source->tag_count);

	return TRUE;
}

static void spawn_source_unload_tags(
	struct spawn_source *source)
{
	if (source->tags)
	{
		tags_header_deregister_vertex_and_index_buffers((struct cache_file_tag_header *)source->tags);
		XPhysicalFree(source->tags);
	}
	if (source->global_indices)
		free(source->global_indices);
	source->tags = NULL;
	source->global_indices = NULL;
	source->tag_count = 0;
	source->tags_failed = FALSE;
}

long spawn_tags_find(
	unsigned long group_tag,
	char const *path)
{
	long count;
	struct spawn_tag_instance const *instances = cache_files_tag_instances(&count);
	long index;

	for (index = 0; instances && index < count; index++)
	{
		if (instances[index].tag_index != NONE && instances[index].group_tag == group_tag && instances[index].name &&
			spawn_same_name(instances[index].name, path))
		{
			return instances[index].tag_index;
		}
	}

	return NONE;
}

/* the source tag to load, and to remap: its global index found (the
playing map's of the same group and name), none (one never loaded), or to
be loaded */
static void spawn_closure_add(
	struct spawn_walk *walk,
	long absolute_index)
{
	struct spawn_source *source = walk->source;
	struct spawn_tag_instance const *instance =
		&((struct spawn_tag_header const *)source->tags)->instances[absolute_index];
	long existing;

	if (source->global_indices[absolute_index] != SPAWN_TAG_UNKNOWN)
		return;
	/* (a map's own tag is the playing map's of that name: one of the same
	name otherwise may be another version, which the rest of the source
	map's tags do not fit: a campaign scorpion's seats are not the
	multiplayer one's animations) */
	if (spawn_group_is_maps_own(instance->group_tag))
	{
		existing = instance->name && instance->name[0] ? spawn_tags_find(instance->group_tag, instance->name) : NONE;
		source->global_indices[absolute_index] = existing;
		return;
	}
	if (!spawn_schema_group(instance->group_tag) || !instance->base_address || walk->found_count >= walk->found_maximum)
	{
		source->global_indices[absolute_index] = NONE;
		return;
	}
	source->global_indices[absolute_index] = SPAWN_TAG_PENDING;
	walk->found[walk->found_count++] = absolute_index;
}

static long spawn_tag_index_at(
	long absolute_index)
{
	return (long)((((unsigned long)(0xC000 + absolute_index) & 0xFFFF) << 16) | (unsigned long)absolute_index);
}

/* the tag table with count places from first_index taken by the source's
tags found: FALSE if they are not free */
static boolean spawn_table_add(
	struct spawn_source *source,
	short source_index,
	long first_index,
	long const *found,
	long count)
{
	long existing_count;
	struct spawn_tag_instance *existing = cache_files_tag_instances(&existing_count);
	struct spawn_tag_instance *grown;
	long grown_count;
	long index;

	if (!existing)
		return FALSE;
	if (!spawn_tags.table)
	{
		spawn_tags.map_table = existing;
		spawn_tags.map_table_count = existing_count;
	}
	if (spawn_tags.map_table_count > SPAWN_TAGS_FIRST_INDEX || first_index < SPAWN_TAGS_FIRST_INDEX ||
		first_index > SPAWN_TAGS_LAST_INDEX + 1 - count)
	{
		return FALSE;
	}
	for (index = first_index; index < first_index + count; index++)
	{
		if (index < existing_count && existing[index].tag_index != NONE)
			return FALSE;
	}
	grown_count = MAX(existing_count, first_index + count);
	grown = malloc((size_t)grown_count * sizeof(*grown));
	if (!grown)
		return FALSE;
	memcpy(grown, existing, (size_t)existing_count * sizeof(*grown));
	for (index = existing_count; index < grown_count; index++)
	{
		memset(&grown[index], 0, sizeof(grown[index]));
		grown[index].group_tag = NONE;
		grown[index].parent_group_tags[0] = NONE;
		grown[index].parent_group_tags[1] = NONE;
		grown[index].tag_index = NONE;
		grown[index].name = "";
	}
	for (index = 0; index < count; index++)
	{
		struct spawn_tag_instance const *instance =
			&((struct spawn_tag_header const *)source->tags)->instances[found[index]];
		struct spawn_tag_instance *added = &grown[first_index + index];

		*added = *instance;
		added->tag_index = spawn_tag_index_at(first_index + index);
		spawn_tags.owners[first_index + index - SPAWN_TAGS_FIRST_INDEX] = (signed char)source_index;
	}
	cache_files_set_tag_instances(grown, grown_count);
	if (spawn_tags.table)
		free(spawn_tags.table);
	spawn_tags.table = grown;
	spawn_tags.table_count = grown_count;
	spawn_tags.next_index = MAX(spawn_tags.next_index, first_index + count);

	return TRUE;
}

/* a loaded sound's permutations name it (xbox_sound_cache.c reads its
samples by that tag) */
static void spawn_sound_set_tag(
	void *base,
	long tag_index)
{
	struct sound_definition *sound = base;
	long range_index;

	for (range_index = 0; range_index < sound->pitch_ranges.count; range_index++)
	{
		struct sound_pitch_range *range = (struct sound_pitch_range *)sound->pitch_ranges.address + range_index;
		long permutation_index;

		for (permutation_index = 0; permutation_index < range->permutations.count; permutation_index++)
		{
			struct sound_permutation *permutation =
				(struct sound_permutation *)range->permutations.address + permutation_index;

			permutation->unknown2 = (unsigned long)tag_index;
			permutation->unknown3 = (unsigned long)tag_index;
		}
	}
}

short spawn_tags_request(
	unsigned long group_tag,
	char const *path,
	char const *source_name,
	unsigned long source_checksum,
	long first_index,
	long *tag_index,
	struct spawn_tags_load *load,
	char *message,
	short message_size)
{
	short source_index;
	struct spawn_source *source;
	struct spawn_tag_header const *header;
	struct spawn_walk walk;
	long root = NONE;
	long index;
	long found[4096];

	*tag_index = spawn_tags_find(group_tag, path);
	memset(load, 0, sizeof(*load));
	message[0] = 0;
	if (*tag_index != NONE)
		return _spawn_tags_loaded;
	if (!spawn_tags.listed)
		spawn_sources_list();
	source_index = spawn_source_named(source_name);
	source = source_index != NONE ? &spawn_tags.sources[source_index] : NULL;
	if (!source)
	{
		snprintf(message, message_size, "No map %s to load %s from", source_name, spawn_last_component(path));
		return _spawn_tags_failed;
	}
	if (source->header.checksum != source_checksum)
	{
		snprintf(message, message_size, "This %s.map is not the host's", source_name);
		return _spawn_tags_failed;
	}
	if (!spawn_source_open_prepared(source))
	{
		if (source->prepare_state == _prepare_failed)
		{
			snprintf(message, message_size, "%s.map could not be decompressed", source_name);
			return _spawn_tags_failed;
		}
		spawn_tags.prepare_wanted = source_index;
		snprintf(message, message_size, "Reading %s.map...", source_name);
		return _spawn_tags_pending;
	}
	if (!spawn_source_load_tags(source, message, message_size))
		return _spawn_tags_failed;

	header = (struct spawn_tag_header const *)source->tags;
	for (index = 0; index < source->tag_count && root == NONE; index++)
	{
		if (header->instances[index].group_tag == group_tag && header->instances[index].name &&
			spawn_same_name(header->instances[index].name, path))
		{
			root = index;
		}
	}
	if (root == NONE)
	{
		snprintf(message, message_size, "%s.map has no %s", source_name, spawn_last_component(path));
		return _spawn_tags_failed;
	}

	/* every tag it needs that the playing map has not, in order */
	memset(&walk, 0, sizeof(walk));
	walk.kind = _walk_closure;
	walk.source = source;
	walk.tags = source->tags;
	walk.size = (unsigned long)source->header.tag_data_size;
	walk.found = found;
	walk.found_maximum = NUMBEROF(found);
	spawn_closure_add(&walk, root);
	for (index = 0; index < walk.found_count && !walk.failed; index++)
	{
		struct spawn_tag_instance const *instance = &header->instances[found[index]];

		spawn_walk_fields(&walk, instance->base_address, spawn_schema_group(instance->group_tag)->definition, 0);
	}
	if (walk.failed || walk.found_count >= walk.found_maximum)
	{
		for (index = 0; index < walk.found_count; index++)
			source->global_indices[found[index]] = SPAWN_TAG_UNKNOWN;
		snprintf(message, message_size, "%s needs too many tags", spawn_last_component(path));
		return _spawn_tags_failed;
	}

	if (first_index == NONE)
		first_index = MAX(spawn_tags.next_index, SPAWN_TAGS_FIRST_INDEX);
	if (spawn_tags.load_count >= MAXIMUM_SPAWN_LOADS ||
		!spawn_table_add(source, source_index, first_index, found, walk.found_count))
	{
		for (index = 0; index < walk.found_count; index++)
			source->global_indices[found[index]] = SPAWN_TAG_UNKNOWN;
		snprintf(message, message_size, "No room for %s's tags", spawn_last_component(path));
		return _spawn_tags_failed;
	}
	for (index = 0; index < walk.found_count; index++)
		source->global_indices[found[index]] = spawn_tag_index_at(first_index + index);

	/* their references the playing map's */
	walk.kind = _walk_remap;
	for (index = 0; index < walk.found_count; index++)
	{
		struct spawn_tag_instance const *instance = &header->instances[found[index]];

		spawn_walk_fields(&walk, instance->base_address, spawn_schema_group(instance->group_tag)->definition, 0);
		if (instance->group_tag == SOUND_DEFINITION_TAG)
			spawn_sound_set_tag(instance->base_address, spawn_tag_index_at(first_index + index));
	}

	load->first_index = first_index;
	load->count = walk.found_count;
	load->group_tag = group_tag;
	strcpy(load->source, source->name);
	load->source_checksum = source->header.checksum;
	snprintf(load->path, sizeof(load->path), "%s", path);
	spawn_tags.loads[spawn_tags.load_count++] = *load;
	*tag_index = source->global_indices[root];
	platform_log("spawn menu: %s loaded from %s with %ld tags at %ld", path, source->name, walk.found_count,
		first_index);

	return _spawn_tags_loaded;
}

long spawn_tags_load_count(
	void)
{
	return spawn_tags.load_count;
}

struct spawn_tags_load const *spawn_tags_load_get(
	long index)
{
	return index >= 0 && index < spawn_tags.load_count ? &spawn_tags.loads[index] : NULL;
}

boolean spawn_tags_read(
	long tag_index,
	long offset,
	long size,
	void *buffer,
	boolean *failed)
{
	short absolute_index = (short)tag_index;
	struct spawn_source *source;
	signed char owner;

	if (absolute_index < SPAWN_TAGS_FIRST_INDEX || absolute_index > SPAWN_TAGS_LAST_INDEX || !spawn_tags.table)
		return FALSE;
	owner = spawn_tags.owners[absolute_index - SPAWN_TAGS_FIRST_INDEX];
	if (owner < 0 || owner >= spawn_tags.source_count)
		return FALSE;
	source = &spawn_tags.sources[owner];
	*failed = offset < 0 || size < 0 || offset > source->header.file_length - size ||
		source->prepared == INVALID_HANDLE_VALUE ||
		!spawn_read_at(source->prepared, (unsigned long)offset, buffer, (unsigned long)size);

	return TRUE;
}

/* ---------- saved games */

#define SPAWN_TAGS_SAVED_SIGNATURE "spawn saved 1"
/* how long a saved game's load waits on its map's decompression (milliseconds) */
#define SPAWN_TAGS_SAVED_WAIT 120000

static void spawn_saved_path(
	char *path,
	unsigned long size)
{
	char const *map = global_scenario_index != NONE ? tag_name_strip_path(tag_get_name(global_scenario_index)) : "";

	snprintf(path, size, "%s\\saved_%.64s.txt", SPAWN_TAGS_FOLDER, map);
}

void spawn_tags_before_save(
	void)
{
	char path[256];
	FILE *file;
	long index;

	spawn_saved_path(path, sizeof(path));
	if (!spawn_tags.load_count)
	{
		DeleteFileA(path);
		return;
	}
	CreateDirectoryA(SPAWN_TAGS_FOLDER, NULL);
	file = fopen(path, "w");
	if (!file)
		return;
	fprintf(file, "%s\n", SPAWN_TAGS_SAVED_SIGNATURE);
	for (index = 0; index < spawn_tags.load_count; index++)
	{
		struct spawn_tags_load const *load = &spawn_tags.loads[index];

		fprintf(file, "load %ld %ld %08lx %08lx %s %s\n", load->first_index, load->count, load->group_tag,
			load->source_checksum, load->source, load->path);
	}
	fclose(file);
}

/* the saved game's loads this game has not made, made at their places */
static void spawn_saved_loads_make(
	void)
{
	char line[SPAWN_TAGS_PATH_LENGTH + 96];
	char path[256];
	FILE *file;

	spawn_saved_path(path, sizeof(path));
	file = fopen(path, "r");
	if (!file)
		return;
	if (!fgets(line, sizeof(line), file) ||
		strncmp(line, SPAWN_TAGS_SAVED_SIGNATURE, strlen(SPAWN_TAGS_SAVED_SIGNATURE)))
	{
		fclose(file);
		return;
	}
	while (fgets(line, sizeof(line), file))
	{
		struct spawn_tags_load load;
		char source[SPAWN_TAGS_MAP_NAME_LENGTH];
		long tag_index;
		long count;
		unsigned long group_tag, checksum;
		long first_index;
		int used = 0;
		char message[96];
		unsigned long start = spawn_now();
		short result;

		line[strcspn(line, "\r\n")] = 0;
		if (sscanf(line, "load %ld %ld %lx %lx %31s %n", &first_index, &count, &group_tag, &checksum, source,
			&used) != 5 || used <= 0 || !line[used] || strlen(line + used) >= SPAWN_TAGS_PATH_LENGTH ||
			spawn_tags_find(group_tag, line + used) != NONE)
		{
			continue;
		}
		do
		{
			result = spawn_tags_request(group_tag, line + used, source, checksum, first_index, &tag_index, &load,
				message, sizeof(message));
			if (result == _spawn_tags_pending)
				spawn_tags_update(TRUE);
		} while (result == _spawn_tags_pending && spawn_now() - start < SPAWN_TAGS_SAVED_WAIT);
		if (result != _spawn_tags_loaded)
			platform_log("spawn menu: the saved game's %s could not be loaded: %s", line + used, message);
		(void)count;
	}
	fclose(file);
}

/* whether the tag index is of a loaded tag the table has not got (its
saved game's load lost) */
static boolean spawn_tag_lost(
	struct spawn_tag_instance const *instances,
	long count,
	long tag_index)
{
	short absolute_index = (short)tag_index;

	return tag_index != NONE && absolute_index >= SPAWN_TAGS_FIRST_INDEX &&
		(!instances || absolute_index >= count || instances[absolute_index].tag_index != tag_index);
}

/* the data's datums of a lost tag, or of an object that is gone, let go
by their own function (each datum's definition and object at those
offsets, NONE for none: their structures are their units' own) */
static void spawn_sweep_data(
	struct data_array *data,
	long definition_offset,
	long object_offset,
	void (*delete_datum)(long))
{
	long count;
	struct spawn_tag_instance const *instances = cache_files_tag_instances(&count);
	long found[512];
	short found_count = 0;
	struct data_iterator iterator;
	byte *datum;
	short index;

	if (!data || !data->valid)
		return;
	data_iterator_new(&iterator, data);
	while ((datum = data_iterator_next(&iterator)) != NULL && found_count < NUMBEROF(found))
	{
		long object_index = object_offset >= 0 ? *(long *)(datum + object_offset) : NONE;

		if (spawn_tag_lost(instances, count, *(long *)(datum + definition_offset)) ||
			(object_index != NONE && !object_try_and_get(object_index)))
		{
			found[found_count++] = iterator.datum_index;
		}
	}
	for (index = 0; index < found_count; index++)
	{
		if (datum_try_and_get(data, found[index]))
			delete_datum(found[index]);
	}
}

void spawn_tags_after_load(
	void)
{
	long count;
	struct spawn_tag_instance const *instances;
	long deleted[256];
	short deleted_count = 0;
	struct object_iterator iterator;
	struct object_datum *object;
	short index;

	spawn_saved_loads_make();
	/* (an object of a tag the table has not got, its tag's load lost: it
	would read the empty tag as its definition) */
	instances = cache_files_tag_instances(&count);
	object_iterator_new(&iterator, _object_mask_all, 0);
	for (object = (struct object_datum *)object_iterator_next(&iterator);
		object && deleted_count < NUMBEROF(deleted);
		object = (struct object_datum *)object_iterator_next(&iterator))
	{
		short absolute_index = (short)object->definition_index;

		if (absolute_index >= SPAWN_TAGS_FIRST_INDEX &&
			(!instances || absolute_index >= count || instances[absolute_index].tag_index != object->definition_index))
		{
			deleted[deleted_count++] = iterator.index;
		}
	}
	for (index = 0; index < deleted_count; index++)
	{
		short attachment;

		object = object_try_and_get(deleted[index]);
		if (!object)
			continue;
		/* (its attachments, which its definition, the empty tag, would not
		say it has: they would go on naming it) */
		for (attachment = 0; attachment < MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT; attachment++)
		{
			long attachment_index = object->object.attachment_indices[attachment];

			/* (a slot past those the object had is a light's type with index
			0: only a whole datum index, with its identifier, is one) */
			if (object->object.attachment_types[attachment] != NONE && attachment_index != NONE &&
				(attachment_index & 0xFFFF0000))
			{
				switch (object->object.attachment_types[attachment])
				{
				case _object_attachment_type_light:
					light_delete(attachment_index);
					break;
				case _object_attachment_type_looping_sound:
					game_looping_sound_delete(attachment_index);
					break;
				case _object_attachment_type_effect:
					effect_delete(attachment_index);
					break;
				case _object_attachment_type_contrail:
					contrail_delete(attachment_index);
					break;
				case _object_attachment_type_particle_system:
					particle_system_orphan(attachment_index);
					break;
				}
			}
			object->object.attachment_types[attachment] = NONE;
			object->object.attachment_indices[attachment] = NONE;
		}
		/* (at once: one only marked would be updated this tick, its
		definition the empty tag) */
		object_delete_immediately(deleted[index]);
	}
	if (deleted_count)
	{
		/* (and what else was of their tags, or hung off them: the game state's
		lights, effects, sounds, contrails and particles; offsets in their
		datums, object_lights.c's, effects.c's, game_sound.c's, contrails.h's,
		particle_systems.h's and particles.h's) */
		spawn_sweep_data(light_data, 0x4, 0x2C, light_delete);
		spawn_sweep_data(effect_data, 0x4, 0x3C, effect_delete);
		spawn_sweep_data(game_looping_sound_data, 0xC, 0x10, game_looping_sound_delete);
		spawn_sweep_data(contrail_data, offsetof(struct contrail_datum, definition_index),
			offsetof(struct contrail_datum, object_index), contrail_delete);
		spawn_sweep_data(particle_systems, offsetof(struct particle_system_datum, definition_index),
			offsetof(struct particle_system_datum, object_index), particle_system_orphan);
		spawn_sweep_data(particle_data, offsetof(struct particle_datum, definition_index),
			offsetof(struct particle_datum, object_index), particle_delete);
		platform_log("spawn menu: %d objects of tags the saved game's map lacks deleted; the level starts over",
			deleted_count);
		/* (what is left of them, a decal or a sound playing, is gone with the
		level: the saved game cannot be played without its tags) */
		main_reset_map();
	}
}

/* ---------- the playing map */

long spawn_tags_map_generation(
	void)
{
	return spawn_tags.generation;
}

void spawn_tags_map_loaded(
	void)
{
	spawn_tags.generation++;
	spawn_tags.table = NULL;
	spawn_tags.table_count = 0;
	spawn_tags.next_index = SPAWN_TAGS_FIRST_INDEX;
	spawn_tags.load_count = 0;
	memset(spawn_tags.owners, NONE, sizeof(spawn_tags.owners));
}

void spawn_tags_map_unloaded(
	void)
{
	short index;

	if (spawn_tags.table)
	{
		cache_files_set_tag_instances(spawn_tags.map_table, spawn_tags.map_table_count);
		free(spawn_tags.table);
	}
	for (index = 0; index < spawn_tags.source_count; index++)
		spawn_source_unload_tags(&spawn_tags.sources[index]);
	spawn_tags.table = NULL;
	spawn_tags.table_count = 0;
	spawn_tags.load_count = 0;
	spawn_tags.next_index = SPAWN_TAGS_FIRST_INDEX;
	memset(spawn_tags.owners, NONE, sizeof(spawn_tags.owners));
}
