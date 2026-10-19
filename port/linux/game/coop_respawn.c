/* A death gets one countdown. The host alone chooses its length and spawns
   the player; clients display the timer for that same full salted corpse.
   A negotiated extension leaves upstream's messages and protocol intact. */
#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "memory/data.h"
#include "main/main.h"
#include "network_coop.h"
#include "network_distributed.h"
#include "network_extensions.h"
#include "coop_respawn.h"
#include <string.h>

long config_integer(char const *name);

#define COOP_RESPAWN_MAX_TICKS (60 * TICKS_PER_SECOND)
struct respawn_clock
{
    long player, corpse, remaining, last_tick;
    unsigned long identity;
    byte sounds;
    boolean active, heard;
};
struct respawn_message
{
    struct distributed_message_header header;
    long corpse, remaining;
    unsigned long identity;
    byte player, reserved[3];
};
static struct respawn_clock respawn_clocks[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];

static unsigned long respawn_identity(struct player_datum const *player)
{
    unsigned long hash = 2166136261u;
    short i;
    for (i = 0; i < 12 && player->name[i]; i++)
        hash = (hash ^ player->name[i]) * 16777619u;
    hash = (hash ^ (unsigned short)player->network_player_data.machine_index) * 16777619u;
    return (hash ^ (unsigned short)player->network_player_data.controller_index) * 16777619u;
}
boolean coop_respawn_active(void)
{
    return network_coop_active() && (game_connection() == _game_connection_network_server ||
        network_extensions_allows(NONE, _distributed_message_coop_respawn));
}
void coop_respawn_reset(void)
{
    memset(respawn_clocks, 0, sizeof(respawn_clocks));
}
static struct respawn_clock *respawn_clock_for(long index, struct player_datum *player)
{
    long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(index);
    struct respawn_clock *clock;
    unsigned long identity = respawn_identity(player);
    if (slot < 0 || slot >= NUMBEROF(respawn_clocks)) return NULL;
    clock = &respawn_clocks[slot];
    if (clock->player != index || clock->identity != identity)
    {
        memset(clock, 0, sizeof(*clock));
        clock->player = index;
        clock->identity = identity;
    }
    return clock;
}
void coop_respawn_died(long index)
{
    struct player_datum *player = player_try_and_get(index);
    struct respawn_clock *clock;
    if (!coop_respawn_active() || !player || player->quit_out_of_game ||
        game_connection() != _game_connection_network_server) return;
    clock = respawn_clock_for(index, player);
    if (!clock) return;
    clock->corpse = player->dead_unit_index;
    clock->remaining = PIN(config_integer("network.coop_respawn_time"), 3, 60) * TICKS_PER_SECOND;
    clock->last_tick = game_time_get();
    clock->sounds = 0;
    clock->heard = FALSE;
    clock->active = TRUE;
    player->respawn_timer = clock->remaining;
}
boolean coop_respawn_ready(long index)
{
    struct player_datum *player = player_try_and_get(index);
    struct respawn_clock *clock;
    if (!coop_respawn_active() || !player || player->statistics.deaths == 0) return TRUE;
    clock = respawn_clock_for(index, player);
    return clock && clock->active && clock->corpse == player->dead_unit_index && !clock->remaining;
}
static void respawn_send(long index, struct player_datum *player, struct respawn_clock const *clock)
{
    struct respawn_message message;
    if (player->local_player_index != NONE) return;
    memset(&message, 0, sizeof(message));
    message.corpse = clock->corpse;
    message.remaining = clock->remaining;
    message.identity = clock->identity;
    message.player = (byte)DATUM_INDEX_TO_ABSOLUTE_INDEX(index);
    distributed_send_to_machine_reliably(player->network_player_data.machine_index, &message,
        _distributed_message_coop_respawn, 1, sizeof(message));
}
void coop_respawn_update(void)
{
    struct data_iterator iterator;
    struct player_datum *player;
    long now = game_time_get();
    boolean host = game_connection() == _game_connection_network_server;
    if (!coop_respawn_active()) return;
    data_iterator_new(&iterator, player_data);
    while ((player = data_iterator_next(&iterator)) != NULL)
    {
        struct respawn_clock *clock = respawn_clock_for(iterator.datum_index, player);
        long elapsed, old;
        short i;
        if (!clock) continue;
        if (player->quit_out_of_game) { clock->active = FALSE; continue; }
        if (player->unit_index != NONE)
        {
            /* A timer can arrive just ahead of its death event. Keep it for
               that unit, but never apply it to a later life or reused slot. */
            if (clock->active && clock->corpse != player->unit_index)
            {
                if (player->local_player_index != NONE && clock->heard)
                    game_engine_respawn_sound(TRUE);
                clock->active = FALSE;
            }
            continue;
        }
        if (host && (!clock->active || clock->corpse != player->dead_unit_index) &&
            player->statistics.deaths != 0)
            coop_respawn_died(iterator.datum_index);
        if (!clock->active || clock->corpse != player->dead_unit_index) continue;
        elapsed = now >= clock->last_tick ? now - clock->last_tick : 0;
        clock->last_tick = now;
        old = clock->remaining;
        if (!game_time_get_paused() && !cinematic_in_progress())
            clock->remaining = MAX(0, old - elapsed);
        player->respawn_timer = clock->remaining;
        if (player->local_player_index != NONE)
        {
            for (i = 0; i < 3; i++)
            {
                long threshold = (3 - i) * TICKS_PER_SECOND;
                byte bit = (byte)(1 << i);
                if (!(clock->sounds & bit) && clock->remaining <= threshold && !cinematic_in_progress())
                {
                    clock->sounds |= bit;
                    /* A late packet skips missed beeps instead of stacking them. */
                    if (clock->remaining > threshold - TICKS_PER_SECOND && !cinematic_in_progress())
                    {
                        game_engine_respawn_sound(FALSE);
                        clock->heard = TRUE;
                    }
                }
            }
        }
        if (host && now % 15 == 0) respawn_send(iterator.datum_index, player, clock);
    }
}
void coop_respawn_handle(long machine, word const *data, word size, boolean reliable)
{
    struct respawn_message message;
    struct player_datum *player;
    struct respawn_clock *clock;
    long index;
    if (!reliable || machine != NONE || size != sizeof(message) ||
        game_connection() != _game_connection_network_client || !game_in_progress() ||
        main_menu_is_active() || !coop_respawn_active()) return;
    memcpy(&message, data, sizeof(message));
    if (GET_MESSAGE_TYPE(message.header.header) != 2 || message.header.count != 1 ||
        message.header.type != _distributed_message_coop_respawn ||
        message.remaining < 0 || message.remaining > COOP_RESPAWN_MAX_TICKS ||
        !distributed_object_index_valid(message.corpse) || message.reserved[0] ||
        message.reserved[1] || message.reserved[2]) return;
    index = distributed_player_from_byte(message.player);
    player = player_try_and_get(index);
    if (!player || player->quit_out_of_game || player->local_player_index == NONE ||
        message.identity != respawn_identity(player) ||
        (message.corpse != player->unit_index && message.corpse != player->dead_unit_index)) return;
    clock = respawn_clock_for(index, player);
    if (!clock) return;
    if (clock->active && clock->corpse == message.corpse)
        clock->remaining = MIN(clock->remaining, message.remaining);
    else
    {
        clock->sounds = 0;
        clock->heard = FALSE;
        clock->remaining = message.remaining;
    }
    clock->active = TRUE;
    clock->corpse = message.corpse;
    clock->last_tick = game_time_get();
}
