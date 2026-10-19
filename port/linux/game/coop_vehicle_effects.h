#ifndef COOP_VEHICLE_EFFECTS_H
#define COOP_VEHICLE_EFFECTS_H
void coop_vehicle_effects_reset(void);
void coop_vehicle_effects_update(void);
void coop_vehicle_respawn_effect(long object_index);
void coop_vehicle_effects_handle(long machine, word const *data, word size, boolean reliable);
#endif
