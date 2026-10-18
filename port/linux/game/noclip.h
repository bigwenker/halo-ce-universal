/*
NOCLIP.H

Noclip (port/linux/game/noclip.c): controls.noclip (N) toggles it for the
keyboard's player, who then flies where they look, through everything. The
host decides who may: its own players may, the others not until the host
allows them (the scoreboard's menu of a player).
*/

#ifndef NOCLIP_H
#define NOCLIP_H

/* each frame of a game (main.c): the key's toggles, and the host's word to
its clients of who flies and who may */
void noclip_update(void);

/* (bipeds.c) a tick of a biped: TRUE if it is a noclipping player's, which
flew where its player looks instead of moving as a biped does */
boolean noclip_biped_move(long biped_index);
/* whether the unit is a noclipping player's (players.c: not stranded) */
boolean noclip_unit_active(long unit_index);
/* whether the player (an absolute index) is noclipping; how fast they fly
(world units a tick) */
boolean noclip_player_active(short player_index);
real noclip_speed(void);

/* the host (the scoreboard's menu): whether this machine decides, whether
the player (an absolute index) may noclip, and allowing or forbidding it */
boolean noclip_host(void);
boolean noclip_player_allowed(short player_index);
void noclip_host_allow(short player_index, boolean allowed);

/* its messages, a fork extension (network_extensions.c) */
boolean network_noclip_handles_message(word const *message, word size);
void network_noclip_handle_message(long machine_index, word const *message, word size);
/* the host: a client has taken the extension, and is told who flies */
void network_noclip_machine_ready(long machine_index);

#endif
