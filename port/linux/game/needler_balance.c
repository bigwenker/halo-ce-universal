/* The fork's Needler rules, in memory only. Unknown/older peers make the
whole game stock; an upstream host never receives a fork rules message. */
#include "cseries.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "main/main.h"
#include "math/real_math.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "items/projectile_definitions.h"
#include "objects/objects.h"
#include "objects/damage_effect_definitions.h"
#include "tag_files/tag_groups.h"
#include "network_distributed.h"
#include "network_extensions.h"
#include "needler_balance.h"
#include <string.h>

unsigned long system_milliseconds(void);

static struct
{
    long weapon_index, needle_index;
    struct weapon_magazine_definition *magazine;
    struct weapon_trigger_definition *trigger;
    struct projectile_definition *needle;
    struct damage_effect_definition *damage[2];
    short loaded, reloaded;
    real rate, inner, lower, upper, initial_velocity, final_velocity, turning;
    real falloff[2], cutoff[2], core[2];
    boolean enabled, client_enabled;
    short sent_policy;
    unsigned long last_sent;
} needler_balance;

struct needler_policy_message
{
    struct distributed_message_header header;
    word enabled, reserved;
};

static void needler_balance_set(boolean enabled)
{
    short i;
    if (!needler_balance.magazine || needler_balance.enabled == enabled) return;
    needler_balance.enabled = enabled;
    needler_balance.magazine->rounds_loaded_maximum = enabled ? 30 : needler_balance.loaded;
    needler_balance.magazine->rounds_reloaded = enabled ? 30 : needler_balance.reloaded;
    needler_balance.trigger->final_rate_of_fire = enabled ? 7.5f : needler_balance.rate;
    needler_balance.trigger->projectile_error_inner_cone_angle = enabled ? 0.f : needler_balance.inner;
    needler_balance.trigger->projectile_error_angle_lower_bound = enabled ? DEGREES_TO_RADIANS(0.5f) : needler_balance.lower;
    needler_balance.trigger->projectile_error_angle_upper_bound = enabled ? DEGREES_TO_RADIANS(1.5f) : needler_balance.upper;
    /* The cache stores velocity in world units per 30 Hz tick, angles in radians/s. */
    needler_balance.needle->projectile.initial_velocity = enabled ? 6.f / TICKS_PER_SECOND : needler_balance.initial_velocity;
    needler_balance.needle->projectile.final_velocity = enabled ? 6.f / TICKS_PER_SECOND : needler_balance.final_velocity;
    needler_balance.needle->projectile.guided_angular_velocity = enabled ? DEGREES_TO_RADIANS(22.5f) : needler_balance.turning;
    for (i = 0; i < 2; i++)
    {
        real scale = enabled ? 0.5f : 1.f;
        needler_balance.damage[i]->falloff_radius = needler_balance.falloff[i] * scale;
        needler_balance.damage[i]->cutoff_radius = needler_balance.cutoff[i] * scale;
        needler_balance.damage[i]->damage.area_of_effect_core_radius = needler_balance.core[i] * scale;
    }
    network_damage_invalidate_tag_cache();
    if (!enabled && game_in_progress())
    {
        struct object_iterator iterator;
        struct weapon_datum *weapon;
        object_iterator_new(&iterator, _object_mask_weapon, 0);
        while ((weapon = (struct weapon_datum *)object_iterator_next(&iterator)) != NULL)
        {
            if (weapon->definition_index == needler_balance.weapon_index &&
                weapon->weapon.magazines[0].rounds_loaded > needler_balance.loaded)
            {
                struct weapon_magazine *state = &weapon->weapon.magazines[0];
                short maximum = MAX(needler_balance.magazine->rounds_total_initial,
                    needler_balance.magazine->rounds_total_maximum);
                state->rounds_total = MIN(maximum, state->rounds_total + state->rounds_loaded - needler_balance.loaded);
                state->rounds_loaded = needler_balance.loaded;
            }
        }
    }
}

void needler_balance_tags_loaded(void)
{
    struct weapon_definition *weapon;
    short i;
    static char const *const damage_names[2] =
    {
        "weapons\\needler\\explosion", "weapons\\needler\\shock wave"
    };
    memset(&needler_balance, 0, sizeof(needler_balance));
    needler_balance.sent_policy = NONE;
    needler_balance.weapon_index = tag_loaded(WEAPON_DEFINITION_TAG, "weapons\\needler\\needler");
    needler_balance.needle_index = tag_loaded(PROJECTILE_DEFINITION_TAG, "weapons\\needler\\needle");
    if (needler_balance.weapon_index == NONE || needler_balance.needle_index == NONE) return;
    weapon = weapon_definition_get(needler_balance.weapon_index);
    if (weapon->weapon.magazines.count < 1 || weapon->weapon.triggers.count < 1) return;
    needler_balance.trigger = TAG_BLOCK_GET_ELEMENT(&weapon->weapon.triggers, 0, struct weapon_trigger_definition);
    if (needler_balance.trigger->projectile.index != needler_balance.needle_index) return;
    for (i = 0; i < 2; i++)
    {
        long index = tag_loaded(DAMAGE_EFFECT_DEFINITION_TAG, damage_names[i]);
        if (index == NONE) return;
        needler_balance.damage[i] = damage_effect_definition_get(index);
        needler_balance.falloff[i] = needler_balance.damage[i]->falloff_radius;
        needler_balance.cutoff[i] = needler_balance.damage[i]->cutoff_radius;
        needler_balance.core[i] = needler_balance.damage[i]->damage.area_of_effect_core_radius;
    }
    needler_balance.needle = projectile_definition_get(needler_balance.needle_index);
    needler_balance.magazine = TAG_BLOCK_GET_ELEMENT(&weapon->weapon.magazines, 0, struct weapon_magazine_definition);
    needler_balance.loaded = needler_balance.magazine->rounds_loaded_maximum;
    needler_balance.reloaded = needler_balance.magazine->rounds_reloaded;
    needler_balance.rate = needler_balance.trigger->final_rate_of_fire;
    needler_balance.inner = needler_balance.trigger->projectile_error_inner_cone_angle;
    needler_balance.lower = needler_balance.trigger->projectile_error_angle_lower_bound;
    needler_balance.upper = needler_balance.trigger->projectile_error_angle_upper_bound;
    needler_balance.initial_velocity = needler_balance.needle->projectile.initial_velocity;
    needler_balance.final_velocity = needler_balance.needle->projectile.final_velocity;
    needler_balance.turning = needler_balance.needle->projectile.guided_angular_velocity;
}

void needler_balance_tags_unloaded(void)
{
    /* No cached pointers may survive the map's tag allocation. */
    memset(&needler_balance, 0, sizeof(needler_balance));
    needler_balance.sent_policy = NONE;
}

short needler_balance_combining_limit(long definition_index, short stock_limit)
{
    /* The original limit excludes the needle currently attaching: 6 means 7. */
    return needler_balance.enabled && definition_index == needler_balance.needle_index ? stock_limit + 1 : stock_limit;
}

void needler_balance_reset(void)
{
    needler_balance.client_enabled = FALSE;
    needler_balance.sent_policy = NONE;
    needler_balance.last_sent = 0;
    needler_balance_set(game_connection() == _game_connection_local);
}

void needler_balance_update(void)
{
    boolean enabled;
    unsigned long now;
    struct needler_policy_message message;
    if (!game_in_progress() || main_menu_is_active()) return;
    enabled = game_connection() == _game_connection_local ||
        (game_connection() == _game_connection_network_server && network_extensions_needler_matches()) ||
        (game_connection() == _game_connection_network_client &&
            network_extensions_allows(NONE, _distributed_message_needler_policy) && needler_balance.client_enabled);
    needler_balance_set(enabled);
    if (game_connection() != _game_connection_network_server) return;
    now = system_milliseconds();
    if (needler_balance.sent_policy == enabled && now - needler_balance.last_sent < 500) return;
    needler_balance.sent_policy = enabled;
    needler_balance.last_sent = now;
    memset(&message, 0, sizeof(message));
    message.enabled = enabled;
    distributed_send(&message, _distributed_message_needler_policy, 1, sizeof(message), _distributed_to_clients_reliably);
}

void needler_balance_handle(long machine, word const *data, word size, boolean reliable)
{
    struct needler_policy_message message;
    if (machine != NONE || !reliable || size != sizeof(message) ||
        game_connection() != _game_connection_network_client || !game_in_progress() || main_menu_is_active() ||
        !network_extensions_allows(NONE, _distributed_message_needler_policy)) return;
    memcpy(&message, data, sizeof(message));
    if (GET_MESSAGE_TYPE(message.header.header) != 2 || message.header.count != 1 ||
        message.header.type != _distributed_message_needler_policy || message.enabled > 1 || message.reserved) return;
    needler_balance.client_enabled = message.enabled != 0;
    needler_balance_update();
}
