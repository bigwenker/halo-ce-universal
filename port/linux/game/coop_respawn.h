/* Timed, host-authoritative network co-op respawns. */
#ifndef COOP_RESPAWN_H
#define COOP_RESPAWN_H
boolean coop_respawn_active(void);
void coop_respawn_reset(void);
void coop_respawn_died(long player_index);
void coop_respawn_update(void);
boolean coop_respawn_ready(long player_index);
void coop_respawn_handle(long machine, word const *data, word size, boolean reliable);
void game_engine_respawn_sound(boolean respawn);
#endif
