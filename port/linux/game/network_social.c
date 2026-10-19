/* Fork co-op scores and chain-reaction damage. Match chat uses the
   upstream network_chat pipeline, including console say. */
#include "cseries.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/players.h"
#include "memory/data.h"
#include "main/console.h"
#include "networking/telnet_console.h"
#include "main/main.h"
#include "network_distributed.h"
#include "network_coop.h"
#include "network_social.h"
#include "network_chat.h"
#include "network_extensions.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "objects/damage.h"
#include "game/game_globals.h"
#include "objects/damage_effect_definitions.h"
#include "units/units.h"
long config_integer(char const *name);
#include "tag_groups.h"
#include "interface/terminal.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define SOCIAL_SCORE_CHUNK 64
/* Match the existing damage categories, without changing tag enums. */
enum { SOCIAL_FALLING = 1, SOCIAL_GRENADE = 3, SOCIAL_VEHICLE = 9 };
struct social_score_entry { byte player, pad[3]; long score; unsigned long identity; };
static struct { long player, score, suicide_unit; } social_players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
static long social_dead_units[HALO_PORT_MAXIMUM_OBJECTS_PER_MAP];
static unsigned long social_sent_scores, social_received_scores;

static boolean social_host(void) { return game_connection() == _game_connection_network_server; }
static long social_slot(long index) { return index == NONE ? NONE : (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(index); }
static boolean social_present(struct player_datum *p) { return p && !p->quit_out_of_game; }
static unsigned long social_identity(struct player_datum const *p)
{
    unsigned long hash = 2166136261u;
    int i;
    for (i = 0; i < 12 && p->name[i]; i++) hash = (hash ^ p->name[i]) * 16777619u;
    return (hash ^ (unsigned short)p->network_player_data.machine_index) * 16777619u;
}
static void social_header(struct distributed_message_header *h, byte kind, byte count)
{
    memset(h, 0, sizeof(*h)); h->header = 8; /* data message; distributed_send fills the length */ h->type = kind; h->count = count; h->game_time = game_time_get();
}
long network_social_score(long index)
{
    long slot = social_slot(index);
    if (slot < 0 || slot >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS || !social_present(player_try_and_get(index))) return 0;
    if (social_players[slot].player != index) {
        social_players[slot].player = index; social_players[slot].score = 0; social_players[slot].suicide_unit = NONE;
    }
    return social_players[slot].score;
}
void network_social_reset(void)
{
    int i;
    for (i = 0; i < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; i++) {
        social_players[i].player = NONE; social_players[i].score = 0; social_players[i].suicide_unit = NONE;
    }
    for (i = 0; i < HALO_PORT_MAXIMUM_OBJECTS_PER_MAP; i++) social_dead_units[i] = NONE;
    social_sent_scores = social_received_scores = 0;
}
void network_social_reverted(void)
{
    int i;
    for(i=0;i<HALO_PORT_MAXIMUM_OBJECTS_PER_MAP;i++) social_dead_units[i]=NONE;
    for(i=0;i<HALO_PORT_MAXIMUM_NETWORK_PLAYERS;i++) social_players[i].suicide_unit=NONE;
}
boolean network_social_chat_send(char const *text)
{
    long index = local_player_get_player_index(0);
    if (!text || !game_in_progress() || main_menu_is_active() ||
        (!social_host() && game_connection() != _game_connection_network_client) ||
        !social_present(player_try_and_get(index))) return FALSE;
    network_chat_say(_chat_channel_everyone, text);
    return TRUE;
}
static boolean social_command_prefix(char const *text, char const *prefix)
{
    while (*prefix)
        if (tolower((unsigned char)*text++) != (unsigned char)*prefix++) return FALSE;
    return TRUE;
}
boolean network_social_console_command(char const *expression, boolean *success)
{
    char const *text = expression;
    while (*text == ' ' || *text == '\t') text++;
    if (!social_command_prefix(text, "say ")) return FALSE;
    *success = network_social_chat_send(text + 4);
    return TRUE;
}

/* Classify retail bipeds/vehicles by their definition, never by a client
   supplied label. Unknown custom tags get a neutral cause instead of a guess. */
static char const *social_unit_kind(char const *tag)
{
    if (strstr(tag,"wraith")) return "Wraith Tank";
    if (strstr(tag,"scorpion")) return "Scorpion Tank";
    if (strstr(tag,"warthog")) return "Warthog";
    if (strstr(tag,"ghost")) return "Ghost";
    if (strstr(tag,"banshee")) return "Banshee";
    if (strstr(tag,"flood") && strstr(tag,"combat")) return "Combat Flood";
    if (strstr(tag,"flood") && strstr(tag,"carrier")) return "Carrier Flood";
    if (strstr(tag,"flood") && strstr(tag,"infection")) return "Infection Flood";
    if (strstr(tag,"grunt")) return "Grunt";
    if (strstr(tag,"elite")) return "Elite";
    if (strstr(tag,"hunter")) return "Hunter";
    if (strstr(tag,"jackal")) return "Jackal";
    if (strstr(tag,"sentinel")) return "Sentinel";
    if (strstr(tag,"marine")) return "Marine";
    return NULL;
}
static long social_points(char const *tag, boolean headshot)
{
    char const *kind = social_unit_kind(tag);
    return 1 + (headshot != FALSE) + (kind && (!strcmp(kind,"Elite") || !strcmp(kind,"Hunter")));
}
void network_social_note_death(long unit_index, struct damage_data const *damage)
{
    struct unit_datum *unit = unit_try_and_get(unit_index);
    struct player_datum *dead, *killer;
    long slot = social_slot(unit_index), killer_index;
    if (!social_host() || !network_coop_active() || !unit || slot < 0 || slot >= HALO_PORT_MAXIMUM_OBJECTS_PER_MAP ||
        social_dead_units[slot] == unit_index || TEST_FLAG(damage->flags, _damage_no_statistics_bit)) return;
    social_dead_units[slot] = unit_index;
    dead = player_try_and_get(unit->unit.player_index);
    if (dead && dead->unit_index == unit_index) return;
    killer_index = damage->owner_player_index;
    killer = player_try_and_get(killer_index);
    if (social_present(killer) && game_team_is_enemy(killer->team_index, unit->object.owner_team_index)) {
        long score = network_social_score(killer_index);
        long points = social_points(tag_get_name(unit->definition_index), TEST_FLAG(damage->flags, _damage_headshot_bit));
        social_players[social_slot(killer_index)].score = score > LONG_MAX - points ? LONG_MAX : score + points;
    }
}
boolean network_social_grenade_chain_reactions_enabled(void)
{
    return !social_host() || !network_coop_active() ||
        config_integer("network.coop_chain_reaction_damage") != 0;
}
real network_social_chain_damage_scale(struct damage_data const *damage, long index)
{
    struct unit_datum *unit;
    long percent;
    if (!social_host() || !network_coop_active() || !TEST_FLAG(damage->flags,_damage_chain_reaction_bit) ||
        !(unit = unit_try_and_get(index)) || !social_present(player_try_and_get(unit->unit.player_index))) return 1.f;
    percent = config_integer("network.coop_chain_reaction_damage");
    return percent == 75 || percent == 50 || percent == 25 ? percent * .01f : 1.f;
}
void network_social_update(void)
{
    unsigned long now = system_milliseconds();
    struct data_iterator it;
    struct player_datum *p;
    struct { struct distributed_message_header header; struct social_score_entry entries[SOCIAL_SCORE_CHUNK]; } message;
    if (!social_host() || !network_coop_active() || (social_sent_scores && now - social_sent_scores < 1000)) return;
    social_sent_scores = now;
    memset(&message,0,sizeof(message)); social_header(&message.header,_distributed_message_social_scores,0);
    data_iterator_new(&it,player_data);
    while ((p = data_iterator_next(&it)) != NULL) {
        struct social_score_entry *entry;
        if (!social_present(p)) continue;
        entry = &message.entries[message.header.count++];
        entry->player = (byte)social_slot(it.datum_index); entry->score = network_social_score(it.datum_index); entry->identity = social_identity(p);
        if (message.header.count == SOCIAL_SCORE_CHUNK) {
            distributed_send(&message,_distributed_message_social_scores,message.header.count,sizeof(message),_distributed_to_clients);
            message.header.count = 0;
        }
    }
    if (message.header.count) distributed_send(&message,_distributed_message_social_scores,message.header.count,
        sizeof(message.header)+message.header.count*sizeof(message.entries[0]),_distributed_to_clients);
}
boolean network_social_handles_message(word const *message, word size)
{
    struct distributed_message_header h;
    if (size < sizeof(h)) return FALSE;
    memcpy(&h,message,sizeof(h));
    return h.type >= _distributed_message_social_request && h.type <= _distributed_message_social_scores;
}
void network_social_handle_message(long machine, word const *message, word size)
{
    struct distributed_message_header h;
    byte const *payload = (byte const *)message + sizeof(h);
    if (!network_social_handles_message(message,size) || !game_in_progress() || main_menu_is_active()) return;
    memcpy(&h,message,sizeof(h));
    if (GET_MESSAGE_TYPE(h.header) != 2) return;
    /* Retired fork chat slots stay reserved; never display them or execute
       their text. Only upstream chat messages can enter the chat pipeline. */
    if (h.type != _distributed_message_social_scores || machine != NONE ||
        game_connection() != _game_connection_network_client) return;
    {
        int i;
        if (!network_coop_active() || h.count > SOCIAL_SCORE_CHUNK || size != sizeof(h)+h.count*sizeof(struct social_score_entry) ||
            (social_received_scores && (long)((unsigned long)h.game_time-social_received_scores)<0)) return;
        social_received_scores = h.game_time;
        for (i=0;i<h.count;i++) {
            struct social_score_entry entry;
            long index;
            struct player_datum *p;
            memcpy(&entry,payload+i*sizeof(entry),sizeof(entry));
            index=distributed_player_from_byte(entry.player);p=player_try_and_get(index);
            if (!social_present(p) || entry.score<0 || entry.identity!=social_identity(p)) continue;
            network_social_score(index);social_players[social_slot(index)].score=entry.score;
        }
    }
}
