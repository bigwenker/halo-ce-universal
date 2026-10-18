#ifndef NETWORK_SOCIAL_H
#define NETWORK_SOCIAL_H
#include "cseries.h"
struct damage_data;
void network_social_reset(void);
void network_social_update(void);
boolean network_social_handles_message(word const *message, word size);
void network_social_handle_message(long machine, word const *message, word size);
boolean network_social_console_command(char const *expression, boolean *success);
boolean network_social_chat_send(char const *text);
void network_social_note_death(long unit_index, struct damage_data const *damage);
long network_social_score(long player_index);
real network_social_chain_damage_scale(struct damage_data const *damage, long object_index);
void network_social_reverted(void);
#endif
