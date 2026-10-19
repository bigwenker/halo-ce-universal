/* Campaign caches omit multiplayer's two respawn sounds. Borrow their
   Xbox ADPCM samples from an installed stock multiplayer cache, once, and
   expose ordinary sound tags. No extracted game assets are shipped. */
#include "cseries.h"
#include "cache/cache_files.h"
#include "game/game_globals.h"
#include "scenario/scenario.h"
#include "sound/game_sound.h"
#include "sound/sound_definitions.h"
#include "tag_files/tag_groups.h"
#include "../../third_party/zlib/zlib_prefixed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);
struct respawn_tag {long group, parents[2], index; char *name; void *data; long unused[2];};
static struct respawn_tag *respawn_table, *respawn_original;
static long respawn_original_count, respawn_sound_tags[2] = {NONE, NONE};
static struct sound_definition respawn_definitions[2];
static struct sound_pitch_range respawn_ranges[2];
static struct sound_permutation respawn_permutations[2];
static char const *respawn_sound_names[2] = {
    "sound\\sfx\\ui\\countdown_for_respawn", "sound\\sfx\\ui\\player_respawn"};
static byte *respawn_samples[2];
static long respawn_sample_sizes[2];
static boolean respawn_audio_tried;

/* The game debug allocator does not accept free(NULL). */
static void audio_release(void *memory) { if (memory) free(memory); }

static unsigned long audio_u32(byte const *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 |
        (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}
static byte const *audio_pointer(byte const *tags, unsigned long size, unsigned long address, unsigned long bytes)
{
    unsigned long offset = address - 0x803a6000UL;
    return offset <= size && bytes <= size - offset ? tags + offset : NULL;
}
static boolean audio_samples(byte const *map, unsigned long size, short which)
{
    unsigned long offset = audio_u32(map + 16), bytes = audio_u32(map + 20), count, i;
    byte const *tags, *instances;
    if (offset > size || bytes > size - offset || bytes < 36) return FALSE;
    tags = map + offset;
    count = audio_u32(tags + 12);
    if (count > 32767) return FALSE;
    instances = audio_pointer(tags, bytes, audio_u32(tags), count * 32);
    if (!instances) return FALSE;
    for (i = 0; i < count; i++)
    {
        byte const *row = instances + i * 32, *name, *sound, *range, *permutation;
        unsigned long sample_offset, sample_size;
        if (audio_u32(row) != 'snd!') continue;
        name = audio_pointer(tags, bytes, audio_u32(row + 16), strlen(respawn_sound_names[which]) + 1);
        if (!name || memcmp(name, respawn_sound_names[which], strlen(respawn_sound_names[which]) + 1)) continue;
        sound = audio_pointer(tags, bytes, audio_u32(row + 20), 164);
        if (!sound || audio_u32(sound + 152) != 1 || sound[6] || sound[7] || sound[108] || sound[109]) return FALSE;
        range = audio_pointer(tags, bytes, audio_u32(sound + 156), 72);
        if (!range || audio_u32(range + 60) != 1) return FALSE;
        permutation = audio_pointer(tags, bytes, audio_u32(range + 64), 124);
        if (!permutation || permutation[40] != 1 || permutation[41] ||
            permutation[42] != 255 || permutation[43] != 255) return FALSE;
        sample_size = audio_u32(permutation + 64);
        sample_offset = audio_u32(permutation + 72);
        if (!sample_size || sample_size > 1048576 || sample_size % 36 ||
            sample_offset > size || sample_size > size - sample_offset) return FALSE;
        respawn_samples[which] = malloc(sample_size);
        if (!respawn_samples[which]) return FALSE;
        memcpy(respawn_samples[which], map + sample_offset, sample_size);
        respawn_sample_sizes[which] = sample_size;
        return TRUE;
    }
    return FALSE;
}
static void audio_load_samples(void)
{
    char path[1024];
    char const *maps[] = {"bloodgulch", "beavercreek", "hangemhigh"};
    short i;
    if (respawn_audio_tried) return;
    respawn_audio_tried = TRUE;
    for (i = 0; i < NUMBEROF(maps); i++)
    {
        FILE *file;
        byte *disk = NULL, *map = NULL;
        long length;
        unsigned long expanded;
        uLongf output;
        boolean okay = FALSE;
        snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), maps[i]);
        file = fopen(path, "rb");
        if (!file) continue;
        if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 2048 || length > 64 * 1024 * 1024 ||
            fseek(file, 0, SEEK_SET)) {fclose(file); continue;}
        disk = malloc(length);
        if (!disk || fread(disk, 1, length, file) != length) {audio_release(disk); fclose(file); continue;}
        fclose(file);
        expanded = audio_u32(disk + 8);
        if (audio_u32(disk) == 'head' && audio_u32(disk + 4) == 5 &&
            expanded >= 2048 && expanded <= 96 * 1024 * 1024)
        {
            if (expanded == (unsigned long)length) map = disk;
            else
            {
                map = malloc(expanded);
                output = expanded - 2048;
                if (map && uncompress(map + 2048, &output, disk + 2048, length - 2048) == Z_OK &&
                    output == expanded - 2048) memcpy(map, disk, 2048);
                else {audio_release(map); map = NULL;}
            }
            if (map) okay = audio_samples(map, expanded, 0) && audio_samples(map, expanded, 1);
        }
        if (map != disk) audio_release(map);
        audio_release(disk);
        if (okay) {platform_log("co-op respawn audio: loaded multiplayer samples from %s", path); return;}
        audio_release(respawn_samples[0]); audio_release(respawn_samples[1]);
        respawn_samples[0] = respawn_samples[1] = NULL;
    }
}
void coop_respawn_audio_loaded(void)
{
    short i;
    long count;
    struct respawn_tag *table;
    long globals_index = tag_loaded('matg', "globals\\globals");
    struct game_globals *globals = globals_index != NONE ? tag_get('matg', globals_index) : NULL;
    if (!globals || globals->multiplayer_information.count) return;
    audio_load_samples();
    if (!respawn_samples[0] || !respawn_samples[1]) {platform_log("co-op respawn audio: no supported stock multiplayer sounds in %s", cache_files_map_directory()); return;}
    table = cache_files_tag_instances(&count);
    if (!table || count > 32765 || !(respawn_table = malloc((count + 2) * sizeof(*table)))) return;
    respawn_original = table; respawn_original_count = count;
    memcpy(respawn_table, table, count * sizeof(*table));
    for (i = 0; i < 2; i++)
    {
        struct sound_definition *sound = &respawn_definitions[i];
        struct sound_pitch_range *range = &respawn_ranges[i];
        struct sound_permutation *permutation = &respawn_permutations[i];
        struct respawn_tag *tag = &respawn_table[count + i];
        memset(sound, 0, sizeof(*sound)); memset(range, 0, sizeof(*range)); memset(permutation, 0, sizeof(*permutation));
        respawn_sound_tags[i] = 0x6e000000L | (count + i);
        sound->sound_class = 50;
        sound->compression = 1;
        sound->random_pitch_bounds.lower = sound->random_pitch_bounds.upper = 1.f;
        sound->inner_cone_angle = sound->outer_cone_angle = 2.f * _pi;
        sound->outer_cone_gain = sound->gain_modifier = 1.f;
        sound->zero_gain_modifier = sound->one_gain_modifier = 1.f;
        sound->zero_pitch_modifier = sound->one_pitch_modifier = 1.f;
        sound->promotion_sound.group_tag = 'snd!'; sound->promotion_sound.index = NONE;
        sound->scripting_sound_index = NONE; sound->scripting_time = (unsigned long)NONE;
        sound->longest_permutation_length = respawn_sample_sizes[i] / 36 * 64 * 1000 / 22050;
        sound->pitch_ranges.count = 1; sound->pitch_ranges.address = range;
        range->natural_pitch = range->playback_rate = range->bend_bounds.upper = 1.f;
        range->actual_permutation_count = 1; range->previous_permutation_index = range->forced_permutation_index = NONE;
        range->permutations.count = 1; range->permutations.address = permutation;
        strcpy(permutation->name, "default"); permutation->gain = 1.f;
        permutation->compression = 1; permutation->next_permutation_index = NONE;
        permutation->unknown0 = NONE; permutation->unknown2 = permutation->unknown3 = respawn_sound_tags[i];
        permutation->samples.size = respawn_sample_sizes[i]; permutation->samples.address = respawn_samples[i];
        memset(tag, 0, sizeof(*tag)); tag->group = 'snd!'; tag->parents[0] = tag->parents[1] = NONE;
        tag->index = respawn_sound_tags[i]; tag->name = (char *)respawn_sound_names[i]; tag->data = sound;
    }
    cache_files_set_tag_instances(respawn_table, count + 2);
}
void coop_respawn_audio_unloaded(void)
{
    if (respawn_original) cache_files_set_tag_instances(respawn_original, respawn_original_count);
    audio_release(respawn_table); respawn_table = respawn_original = NULL;
    respawn_sound_tags[0] = respawn_sound_tags[1] = NONE;
}
boolean coop_respawn_audio_read(struct sound_permutation const *permutation, void *buffer)
{
    short i;
    for (i = 0; i < 2; i++)
        if (respawn_sound_tags[i] != NONE && permutation == &respawn_permutations[i])
        {memcpy(buffer, respawn_samples[i], respawn_sample_sizes[i]); return TRUE;}
    return FALSE;
}
void coop_respawn_audio_play(boolean respawn)
{
    long tag = respawn_sound_tags[respawn != FALSE];
    if (tag != NONE) unspatialized_impulse_sound_new(tag, 1.f);
}
