#ifndef CUSTOM_MODELS_H
#define CUSTOM_MODELS_H
struct bitmap_data;
void custom_models_loaded(void);
void custom_models_unloaded(void);
long custom_models_render_model(long object, long original);
void custom_models_object_created(long object);
void custom_models_player_changed(long player);
void custom_models_selection_changed(void);
boolean custom_models_profile_read(long profile, char id[32]);
boolean custom_models_profile_write(long profile, char const *id);
void custom_models_menu_preview(short model);
struct bitmap_data *custom_models_preview_bitmap(char const *name, struct bitmap_data *original);
void custom_models_preview_forget(void);
char const *custom_models_player_id(long player);
void custom_models_network_reset(void);
void custom_models_network_update(void);
void custom_models_network_handle(long machine, word const *data, word size, boolean reliable);
#endif
