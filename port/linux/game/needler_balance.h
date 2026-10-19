#ifndef NEEDLER_BALANCE_H
#define NEEDLER_BALANCE_H
void needler_balance_tags_loaded(void);
void needler_balance_tags_unloaded(void);
void needler_balance_reset(void);
void needler_balance_update(void);
void needler_balance_handle(long machine, word const *data, word size, boolean reliable);
short needler_balance_combining_limit(long definition_index, short stock_limit);
#endif
