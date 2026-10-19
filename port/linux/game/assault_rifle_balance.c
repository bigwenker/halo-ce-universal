/* Optional fork AR rules. Spread is simulated by the shooter, so mixed
builds must restore both spread and damage rather than share a partial buff. */
#include "cseries.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "main/main.h"
#include "items/weapon_definitions.h"
#include "items/projectile_definitions.h"
#include "objects/damage_effect_definitions.h"
#include "tag_files/tag_groups.h"
#include "network_distributed.h"
#include "network_extensions.h"
#include "assault_rifle_balance.h"
#include <string.h>

unsigned long system_milliseconds(void);

static struct assault_rifle_balance_state
{
    struct weapon_trigger_definition *trigger;
    struct damage_effect_definition *damage;
    real inner, lower, upper, distribution, minimum, damage_lower, damage_upper;
    boolean enabled, client_enabled;
    short sent_policy;
    unsigned long last_sent;
} assault_rifle_balance;

struct assault_rifle_policy_message
{
    struct distributed_message_header header;
    word enabled, reserved;
};

static void assault_rifle_balance_set(boolean enabled)
{
    real spread = enabled ? 0.75f : 1.f;
    real damage = enabled ? 1.2f : 1.f;
    if (!assault_rifle_balance.trigger || assault_rifle_balance.enabled == enabled) return;
    assault_rifle_balance.enabled = enabled;
    assault_rifle_balance.trigger->projectile_error_inner_cone_angle = assault_rifle_balance.inner * spread;
    assault_rifle_balance.trigger->projectile_error_angle_lower_bound = assault_rifle_balance.lower * spread;
    assault_rifle_balance.trigger->projectile_error_angle_upper_bound = assault_rifle_balance.upper * spread;
    assault_rifle_balance.trigger->projectile_distribution_angle = assault_rifle_balance.distribution * spread;
    assault_rifle_balance.damage->damage.damage_minimum = assault_rifle_balance.minimum * damage;
    assault_rifle_balance.damage->damage.damage_lower_bound = assault_rifle_balance.damage_lower * damage;
    assault_rifle_balance.damage->damage.damage_upper_bound = assault_rifle_balance.damage_upper * damage;
    network_damage_invalidate_tag_cache();
}

void assault_rifle_balance_tags_unloaded(void)
{
    memset(&assault_rifle_balance, 0, sizeof(assault_rifle_balance));
    assault_rifle_balance.sent_policy = NONE;
}

void assault_rifle_balance_tags_loaded(void)
{
    long weapon_index, projectile_index, damage_index;
    struct weapon_definition *weapon;
    struct weapon_trigger_definition *trigger;
    struct projectile_definition *projectile;
    struct damage_effect_definition *damage;
    assault_rifle_balance_tags_unloaded();
    weapon_index = tag_loaded(WEAPON_DEFINITION_TAG, "weapons\\assault rifle\\assault rifle");
    projectile_index = tag_loaded(PROJECTILE_DEFINITION_TAG, "weapons\\assault rifle\\bullet");
    damage_index = tag_loaded(DAMAGE_EFFECT_DEFINITION_TAG, "weapons\\assault rifle\\bullet");
    if (weapon_index == NONE || projectile_index == NONE || damage_index == NONE) return;
    weapon = weapon_definition_get(weapon_index);
    if (weapon->weapon.triggers.count < 1) return;
    trigger = TAG_BLOCK_GET_ELEMENT(&weapon->weapon.triggers, 0, struct weapon_trigger_definition);
    projectile = projectile_definition_get(projectile_index);
    if (trigger->projectile.index != projectile_index || projectile->projectile.impact_damage.index != damage_index) return;
    damage = damage_effect_definition_get(damage_index);
    assault_rifle_balance.inner = trigger->projectile_error_inner_cone_angle;
    assault_rifle_balance.lower = trigger->projectile_error_angle_lower_bound;
    assault_rifle_balance.upper = trigger->projectile_error_angle_upper_bound;
    assault_rifle_balance.distribution = trigger->projectile_distribution_angle;
    assault_rifle_balance.minimum = damage->damage.damage_minimum;
    assault_rifle_balance.damage_lower = damage->damage.damage_lower_bound;
    assault_rifle_balance.damage_upper = damage->damage.damage_upper_bound;
    assault_rifle_balance.damage = damage;
    assault_rifle_balance.trigger = trigger;
}

void assault_rifle_balance_reset(void)
{
    assault_rifle_balance.client_enabled = FALSE;
    assault_rifle_balance.sent_policy = NONE;
    assault_rifle_balance.last_sent = 0;
    assault_rifle_balance_set(game_connection() == _game_connection_local);
}

void assault_rifle_balance_update(void)
{
    boolean enabled;
    unsigned long now;
    struct assault_rifle_policy_message message;
    if (!game_in_progress() || main_menu_is_active()) return;
    enabled = game_connection() == _game_connection_local ||
        (game_connection() == _game_connection_network_server &&
            network_extensions_balance_matches(_distributed_message_assault_rifle_policy)) ||
        (game_connection() == _game_connection_network_client &&
            network_extensions_allows(NONE, _distributed_message_assault_rifle_policy) && assault_rifle_balance.client_enabled);
    assault_rifle_balance_set(enabled);
    if (game_connection() != _game_connection_network_server) return;
    now = system_milliseconds();
    if (assault_rifle_balance.sent_policy == enabled && now - assault_rifle_balance.last_sent < 500) return;
    assault_rifle_balance.sent_policy = enabled;
    assault_rifle_balance.last_sent = now;
    memset(&message, 0, sizeof(message));
    message.enabled = enabled;
    distributed_send(&message, _distributed_message_assault_rifle_policy, 1, sizeof(message), _distributed_to_clients_reliably);
}

void assault_rifle_balance_handle(long machine, word const *data, word size, boolean reliable)
{
    struct assault_rifle_policy_message message;
    if (machine != NONE || !reliable || size != sizeof(message) ||
        game_connection() != _game_connection_network_client || !game_in_progress() || main_menu_is_active() ||
        !network_extensions_allows(NONE, _distributed_message_assault_rifle_policy)) return;
    memcpy(&message, data, sizeof(message));
    if (GET_MESSAGE_TYPE(message.header.header) != 2 || message.header.count != 1 ||
        message.header.type != _distributed_message_assault_rifle_policy || message.enabled > 1 || message.reserved) return;
    assault_rifle_balance.client_enabled = message.enabled != 0;
    assault_rifle_balance_update();
}
