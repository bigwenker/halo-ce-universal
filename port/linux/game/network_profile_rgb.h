#ifndef NETWORK_PROFILE_RGB_H
#define NETWORK_PROFILE_RGB_H
void network_profile_rgb_reset(void);
void network_profile_rgb_changed(void);
void network_profile_rgb_machine_joined(long machine);
void network_profile_rgb_update(void);
boolean network_profile_rgb_color(long player, union real_rgb_color *color);
void network_profile_rgb_handle(long machine, word const *data, word size, boolean reliable);
#endif
