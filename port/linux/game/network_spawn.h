/*
NETWORK_SPAWN.H

The spawn menu in a network game (port/linux/game/network_spawn.c): a
client asks the host to spawn, and the host tells its clients of the tags
it loads from other maps for it.
*/

#ifndef NETWORK_SPAWN_H
#define NETWORK_SPAWN_H

struct spawn_tags_entry;
struct spawn_tags_load;

/* the clients' loads waited on, made each frame (spawn_menu_update) */
void network_spawn_update(void);
boolean network_spawn_handles_message(word const *message, word size);
void network_spawn_handle_message(long machine_index, word const *message, word size);
/* the host: a client has taken the spawn extension (network_extensions.c),
and is told of every load so far */
void network_spawn_machine_ready(long machine_index);

/* whether this machine is a client of a network game, which spawns through
its host; whether its host takes the requests */
boolean network_spawn_client(void);
boolean network_spawn_host_available(void);
/* a client's request to the host: the thing, and the view's ray */
boolean network_spawn_request(struct spawn_tags_entry const *entry, union real_point3d const *origin,
	union real_vector3d const *direction);
/* the host: tags it loaded, to every client; what came of a client's
request, to it */
void network_spawn_send_load(struct spawn_tags_load const *load);
void network_spawn_send_result(long machine_index, char const *text);

#endif
