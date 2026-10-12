/* Fork features are optional additions to upstream's unchanged protocol. */
#ifndef NETWORK_EXTENSIONS_H
#define NETWORK_EXTENSIONS_H
void network_extensions_select_host(boolean available);
void network_extensions_reset(void);
void network_extensions_machine_joined(long machine);
void network_extensions_update(void);
void network_extensions_handle(long machine, word const *data, word size, boolean reliable);
word network_extensions_feature(byte type);
boolean network_extensions_allows(long machine, byte type);
#endif
