#ifndef PROFILE_COLOR_MENU_H
#define PROFILE_COLOR_MENU_H
struct bitmap_data;
struct widget_instance;
struct halo_ui_pointer;
void profile_color_menu_begin(long profile, short palette);
void profile_color_menu_cancel(void);
unsigned long profile_color_menu_rgb(void);
void profile_color_menu_adjust(char const *name, short direction);
void profile_color_menu_values(real *h, real *s, real *v);
void profile_color_menu_portrait(long profile);
void profile_color_menu_invalidate(void);
void profile_color_menu_forget(void);
boolean profile_color_menu_pointer(struct halo_ui_pointer const *pointer);
struct bitmap_data *profile_color_menu_bitmap(char const *name, struct bitmap_data *original);
boolean profile_color_menu_bounds(char const *name, union rectangle2d *bounds);
#endif
