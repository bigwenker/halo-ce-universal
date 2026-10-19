#ifndef ASSAULT_RIFLE_BALANCE_H
#define ASSAULT_RIFLE_BALANCE_H
void assault_rifle_balance_tags_loaded(void);
void assault_rifle_balance_tags_unloaded(void);
void assault_rifle_balance_reset(void);
void assault_rifle_balance_update(void);
void assault_rifle_balance_handle(long machine, word const *data, word size, boolean reliable);
#endif
