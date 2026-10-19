/* Never send fork payloads to upstream peers. An unused advertisement bit
   invites a reliable, revisioned capability exchange; it changes neither the
   core message layouts nor their IDs. A machine slot is cleared on every join. */
#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "main/main.h"
#include "network_distributed.h"
#include "network_extensions.h"
#include "network_aim_assist.h"
#include "network_profile_rgb.h"
#include "needler_balance.h"
#include <string.h>

boolean network_distributed_client_send_reliably(void *, word);
short network_distributed_server_machines(long *, short);
enum
{
    EXT_AIM = 1, EXT_SUICIDE = 2, EXT_DEATH = 4, EXT_SOCIAL = 8,
    EXT_SPRAY = 16, EXT_MODELS = 32, EXT_NEEDLER = 64, EXT_ALL = 127, EXT_REVISION = 1
};
#define EXT_MAGIC 0x45544C45UL /* ELTE */
struct extension_message
{
    struct distributed_message_header header;
    unsigned long magic;
    word revision, features;
};
static boolean host_available, hello_sent;
static word host_features, machine_features[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
static unsigned long last_hello;
static short hello_attempts;

void network_extensions_select_host(boolean available)
{
    host_available = available;
    network_extensions_reset();
}

void network_extensions_reset(void)
{
    host_features = 0;
    hello_sent = FALSE;
    last_hello = 0;
    hello_attempts = 0;
    memset(machine_features, 0, sizeof(machine_features));
    needler_balance_reset();
}

void network_extensions_machine_joined(long machine)
{
	network_profile_rgb_machine_joined(machine);
    if (machine >= 0 && machine < HALO_PORT_MAXIMUM_NETWORK_MACHINES)
        machine_features[machine] = 0;
}

word network_extensions_feature(byte type)
{
    switch (type)
    {
    case _distributed_message_aim_assist_policy: return EXT_AIM;
    case _distributed_message_suicide_request: return EXT_SUICIDE;
    case _distributed_message_coop_death: return EXT_DEATH;
    case _distributed_message_social_request:
    case _distributed_message_social_line:
    case _distributed_message_social_scores:
    case 251: return EXT_SOCIAL; /* optional profile RGB; older forks ignore it */
    case 252: return EXT_MODELS; /* optional model selection IDs */
    case 250: return EXT_SPRAY; /* spray_share.h's private extension */
    case _distributed_message_needler_policy: return EXT_NEEDLER;
    default: return 0;
    }
}

/* Unknown peers, including a reused slot, always mean stock rules. */
boolean network_extensions_needler_matches(void)
{
    long machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
    long player_index = local_player_get_player_index(0);
    struct player_datum *player = player_index == NONE ? NULL : player_try_and_get(player_index);
    short i, count;
    if (game_connection() != _game_connection_network_server || !player) return FALSE;
    count = network_distributed_server_machines(machines, NUMBEROF(machines));
    for (i = 0; i < count; i++)
    {
        /* The server's loopback client is in this list too; it needs no hello. */
        if (machines[i] == player->network_player_data.machine_index) continue;
        if (machines[i] < 0 || machines[i] >= HALO_PORT_MAXIMUM_NETWORK_MACHINES ||
            !(machine_features[machines[i]] & EXT_NEEDLER)) return FALSE;
    }
    return TRUE;
}

boolean network_extensions_allows(long machine, byte type)
{
    word feature = network_extensions_feature(type);
    if (!feature) return TRUE;
    if (machine == NONE)
        return game_connection() == _game_connection_network_client &&
            (host_features & feature) != 0;
    return game_connection() == _game_connection_network_server && machine >= 0 &&
        machine < HALO_PORT_MAXIMUM_NETWORK_MACHINES && (machine_features[machine] & feature) != 0;
}

void network_extensions_update(void)
{
    struct extension_message message;
    unsigned long now = system_milliseconds();
    if (game_connection() != _game_connection_network_client || !host_available || host_features ||
        !game_in_progress() || main_menu_is_active() || (hello_sent && now - last_hello < 2000)) return;
    memset(&message, 0, sizeof(message));
    message.header.type = _distributed_message_fork_capabilities;
    message.header.count = 1;
    message.header.game_time = game_time_get();
    message.magic = EXT_MAGIC;
    message.revision = EXT_REVISION;
    /* Older forks reject unknown bits: retry their original feature set. */
    message.features = hello_attempts < 2 ? EXT_ALL : hello_attempts < 4 ? 63 : 31;
    build_message_header(&message.header.header, sizeof(message), 2, 0);
    if (network_distributed_client_send_reliably(&message, sizeof(message)))
    {
        hello_sent = TRUE;
        if (hello_attempts < 4) hello_attempts++;
        last_hello = now;
    }
}

void network_extensions_handle(long machine, word const *data, word size, boolean reliable)
{
    struct extension_message message;
    if (!reliable || size != sizeof(message) || !game_in_progress() || main_menu_is_active()) return;
    memcpy(&message, data, sizeof(message));
    if (GET_MESSAGE_TYPE(message.header.header) != 2 || message.header.count != 1 ||
        message.header.type != _distributed_message_fork_capabilities || message.magic != EXT_MAGIC ||
        message.revision != EXT_REVISION || !message.features || (message.features & ~EXT_ALL)) return;
    if (game_connection() == _game_connection_network_server)
    {
        long machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
        short i, count = network_distributed_server_machines(machines, NUMBEROF(machines));
        if (machine < 0 || machine >= HALO_PORT_MAXIMUM_NETWORK_MACHINES) return;
        for (i = 0; i < count && machines[i] != machine; i++) {}
        if (i == count || machine_features[machine] != 0) return;
        machine_features[machine] = message.features;
        distributed_send_to_machine_reliably(machine, &message,
            _distributed_message_fork_capabilities, 1, sizeof(message));
        network_aim_assist_send_policy(machine);
    }
    else if (game_connection() == _game_connection_network_client && machine == NONE &&
        host_available && hello_sent)
        host_features = message.features;
}
