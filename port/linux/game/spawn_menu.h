/*
SPAWN_MENU.H

The spawn menu (port/linux/game/spawn_menu.c): controls.spawn_menu (F6)
opens it over the game and closes it again.
*/

#ifndef SPAWN_MENU_H
#define SPAWN_MENU_H

struct spawn_tags_entry;

/* whether the menu is open: the mouse is its pointer, not the aim */
boolean spawn_menu_is_open(void);
/* opens or closes it as its key asks, and spawns what was picked
(main.c, each frame of a game in progress) */
void spawn_menu_update(void);
/* draws it and takes its pointer's clicks (interface.c) */
void spawn_menu_draw(void);

/* a spawn to make, where the ray from origin along direction meets a
surface: this machine's (machine_index NONE) or a client's (the host) */
void spawn_menu_queue(long machine_index, struct spawn_tags_entry const *entry, union real_point3d const *origin,
	union real_vector3d const *direction);
/* what the line beneath the boxes says (the host's answer to a request) */
void spawn_menu_note(char const *text);
/* whether the menu is making an object: what was picked, not what a game
type would have instead (objects.c) */
boolean spawn_menu_creating_object(void);

#endif
