/*
HALO_KEYBOARD.H

The keyboard and mouse's own controls (port/linux/src/xinput_sdl.c): in the
game they do not drive the first controller, as they do in the menus, but
the player's actions themselves, by the bindings in config.toml's
[controls] (Settings > Controls Setup changes them). The game takes them
with the first controller's (source/input/input_abstraction.c), so the
profile's button layout is the controller's alone, and reloading has a key
of its own (source/game/player_control.c).
*/

#ifndef HALO_KEYBOARD_H
#define HALO_KEYBOARD_H

enum halo_keyboard_action
{
	HALO_KEYBOARD_MOVE_FORWARD,
	HALO_KEYBOARD_MOVE_BACKWARD,
	HALO_KEYBOARD_STRAFE_LEFT,
	HALO_KEYBOARD_STRAFE_RIGHT,
	HALO_KEYBOARD_JUMP,
	HALO_KEYBOARD_CROUCH,
	HALO_KEYBOARD_FIRE,
	HALO_KEYBOARD_THROW_GRENADE,
	HALO_KEYBOARD_MELEE,
	HALO_KEYBOARD_RELOAD,
	HALO_KEYBOARD_ZOOM,
	HALO_KEYBOARD_SWITCH_WEAPON,
	HALO_KEYBOARD_SWITCH_GRENADE,
	HALO_KEYBOARD_ACTION,
	HALO_KEYBOARD_FLASHLIGHT,
	/* (these two press the controller's Back and Start, which the menus
	read from it) */
	HALO_KEYBOARD_SCOREBOARD,
	HALO_KEYBOARD_PAUSE,
	HALO_KEYBOARD_SCREENSHOT,
	HALO_KEYBOARD_SPRAY,
	/* (voice chat's, which the game itself does not read:
	halo_push_to_talk_held) */
	HALO_KEYBOARD_PUSH_TO_TALK,
	HALO_KEYBOARD_CHAT,
	/* (the spawn menu's: halo_spawn_menu_requested) */
	HALO_KEYBOARD_SPAWN_MENU,
	/* (noclip's: halo_noclip_requested) */
	HALO_KEYBOARD_NOCLIP,
	NUMBER_OF_HALO_KEYBOARD_ACTIONS
};

/* the actions the keyboard and mouse hold for the player on the controller
(a bit for each halo_keyboard_action): theirs is the first's; none while a
menu or the console is up */
unsigned long halo_keyboard_actions(short controller_index);
/* whether voice chat's push to talk is held (controls.push_to_talk), in the
game and in the menus alike */
int halo_push_to_talk_held(void);
int halo_chat_requested(void);
/* whether controls.spawn_menu was pressed since the last call: it opens and
closes the spawn menu (port/linux/game/spawn_menu.c); its pause key, while
the menu is open, closes it too */
int halo_spawn_menu_requested(void);
/* whether controls.noclip was pressed since the last call: it starts and
stops the keyboard's player's noclip (port/linux/game/noclip.c) */
int halo_noclip_requested(void);

#endif
