/* Extra vehicle copies use the campaign's own co-op teleport effect. The
   optional notice may precede object creation; wait for the full handle,
   never substitute whichever object later occupies its absolute slot. */
#include "cseries.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "scenario/scenario.h"
#include "main/main.h"
#include "objects/objects.h"
#include "effects/effects.h"
#include "network_coop.h"
#include "network_distributed.h"
#include "network_extensions.h"
#include "coop_vehicle_effects.h"
#include <string.h>

struct vehicle_effect_message
{
    struct distributed_message_header header;
    long object, definition;
};
static struct vehicle_effect_pending
{
    long object, definition, received;
    boolean used, applied;
} vehicle_effects[64];
static short vehicle_effect_cursor;

void coop_vehicle_effects_reset(void)
{
    memset(vehicle_effects, 0, sizeof(vehicle_effects));
    vehicle_effect_cursor = 0;
}
static boolean vehicle_effect_play(long index, long definition)
{
    struct object_datum *object = object_try_and_get_and_verify_type(index, _object_mask_vehicle);
    struct game_globals *globals;
    struct game_globals_player_information *information;
    long effect;
    if (!object || object->definition_index != definition || TEST_FLAG(object->object.damage_flags, _object_dead_bit)) return FALSE;
    globals = scenario_get_game_globals();
    if (!globals || globals->player_information.count < 1) return TRUE;
    information = TAG_BLOCK_GET_ELEMENT(&globals->player_information, 0, struct game_globals_player_information);
    effect = information->coop_respawn_effect.index;
    if (distributed_tag_of_group(effect, 'effe'))
        effect_new_from_object(effect, index, index, NONE, 0.f, 0.f, NULL, NULL);
    return TRUE;
}
void coop_vehicle_respawn_effect(long index)
{
    struct object_datum *object;
    struct vehicle_effect_message message;
    if (game_connection() != _game_connection_network_server || !network_coop_active()) return;
    object = object_try_and_get_and_verify_type(index, _object_mask_vehicle);
    if (!object) return;
    memset(&message, 0, sizeof(message));
    message.object = index; message.definition = object->definition_index;
    /* No datum pointer survives a call that may allocate an effect/object. */
    vehicle_effect_play(message.object, message.definition);
    distributed_send(&message, _distributed_message_coop_vehicle_effect, 1, sizeof(message), _distributed_to_clients_reliably);
}
void coop_vehicle_effects_handle(long machine, word const *data, word size, boolean reliable)
{
    struct vehicle_effect_message message;
    short i;
    struct vehicle_effect_pending *pending;
    if (!reliable || machine != NONE || size != sizeof(message) ||
        game_connection() != _game_connection_network_client || !network_coop_active() ||
        !game_in_progress() || main_menu_is_active() ||
        !network_extensions_allows(NONE, _distributed_message_coop_vehicle_effect)) return;
    memcpy(&message, data, sizeof(message));
    if (GET_MESSAGE_TYPE(message.header.header) != 2 || message.header.count != 1 ||
        message.header.type != _distributed_message_coop_vehicle_effect ||
        !distributed_object_index_valid(message.object) || !distributed_tag_of_group(message.definition, 'vehi')) return;
    for (i = 0; i < NUMBEROF(vehicle_effects); i++)
        if (vehicle_effects[i].used && vehicle_effects[i].object == message.object) return;
    pending = &vehicle_effects[vehicle_effect_cursor++ % NUMBEROF(vehicle_effects)];
    if (vehicle_effect_cursor == NUMBEROF(vehicle_effects)) vehicle_effect_cursor = 0;
    pending->object = message.object; pending->definition = message.definition;
    pending->received = game_time_get(); pending->used = TRUE; pending->applied = FALSE;
}
void coop_vehicle_effects_update(void)
{
    short i;
    long now = game_time_get();
    if (game_connection() != _game_connection_network_client || !network_coop_active()) return;
    for (i = 0; i < NUMBEROF(vehicle_effects); i++)
    {
        struct vehicle_effect_pending *pending = &vehicle_effects[i];
        if (!pending->used || pending->applied) continue;
        if (now < pending->received || now - pending->received > 3 * TICKS_PER_SECOND ||
            vehicle_effect_play(pending->object, pending->definition)) pending->applied = TRUE;
    }
}
