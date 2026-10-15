#include "player_profile_rgb.h"
#include "saved games/player_profile.h"
#include "interface/player_ui.h"
#include "tag_files/files.h"
#include <stdio.h>
#include <string.h>

/* Joining can release the menu's active profile before the unit is made.
   The remembered profile is the same one campaign/Settings use then. */
static boolean last_profile_valid;
static long last_profile=NONE;
void profile_rgb_local_profile_reset(void) {last_profile_valid=FALSE;last_profile=NONE;}
long profile_rgb_local_profile(short local_player)
{
    long profile=player_ui_get_active_player_profile_index(local_player);
    if(profile==NONE && local_player==0)
    {
        if(!last_profile_valid)
        {last_profile=player_ui_get_player1_last_used_profile_index();last_profile_valid=TRUE;}
        profile=last_profile;
    }
    return profile;
}

boolean profile_rgb_parse(char const *text, unsigned long *rgb)
{
	unsigned long result = 0;
	short i;
	if (!text || *text++ != '#') return FALSE;
	for (i = 0; i < 6; i++)
	{
		unsigned char c = (unsigned char)*text++;
		short digit = c >= '0' && c <= '9' ? c - '0' :
			c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
		if (digit < 0) return FALSE;
		result = (result << 4) | digit;
	}
	if (*text) return FALSE;
	*rgb = result;
	return TRUE;
}

void profile_rgb_format(unsigned long rgb, char text[8])
{
	snprintf(text, 8, "#%06lX", rgb & 0xFFFFFFUL);
}

static boolean profile_rgb_file(long profile, struct file_reference *file)
{
	char directory[256], path[300];
	if (profile == NONE || !player_profile_get_enclosing_directory_path(profile, directory)) return FALSE;
	if (snprintf(path, sizeof(path), "%sprofile_color.txt", directory) >= sizeof(path)) return FALSE;
	return file_reference_create_from_path(file, path, FALSE) != NULL;
}

boolean profile_rgb_read(long profile, unsigned long *rgb)
{
	struct file_reference file;
	char text[8];
	boolean ok = FALSE;
	if (!profile_rgb_file(profile, &file) || !file_open(&file, FLAG(_permission_read_bit))) return FALSE;
	if (file_get_eof(&file) == 7 && file_read(&file, 7, text))
	{
		text[7] = 0;
		ok = profile_rgb_parse(text, rgb);
	}
	file_close(&file);
	return ok;
}

boolean profile_rgb_write(long profile, unsigned long rgb)
{
	struct file_reference file;
	char text[8];
	boolean ok;
	if (!profile_rgb_file(profile, &file) || (!file_exists(&file) && !file_create(&file)) ||
		!file_open(&file, FLAG(_permission_write_bit))) return FALSE;
	profile_rgb_format(rgb, text);
	ok = file_write(&file, 7, text) && file_set_eof(&file, 7);
	if (!file_close(&file)) ok = FALSE;
	return ok;
}

short profile_rgb_nearest(unsigned long rgb)
{
	real_rgb_color color;
	real best = 4.f, distance, r = ((rgb >> 16) & 255) / 255.f;
	real g = ((rgb >> 8) & 255) / 255.f, b = (rgb & 255) / 255.f;
	short i, chosen = 0;
	for (i = 0; i < player_profile_number_of_available_primary_colors(); i++)
	{
		color = player_profile_get_rgb_color(i);
		distance = (r-color.red)*(r-color.red) + (g-color.green)*(g-color.green) + (b-color.blue)*(b-color.blue);
		if (distance < best) { best = distance; chosen = i; }
	}
	return chosen;
}

unsigned long profile_rgb_from_hsv(real h, real s, real v)
{
	real r, g, b, f, p, q, t;
	short sector;
	h = PIN(h, 0.f, 360.f); s = PIN(s, 0.f, 1.f); v = PIN(v, 0.f, 1.f);
	if (h >= 360.f) h = 0.f;
	sector = (short)(h / 60.f); f = h / 60.f - sector;
	p = v*(1.f-s); q = v*(1.f-s*f); t = v*(1.f-s*(1.f-f));
	switch (sector)
	{
	case 0: r=v; g=t; b=p; break;
	case 1: r=q; g=v; b=p; break;
	case 2: r=p; g=v; b=t; break;
	case 3: r=p; g=q; b=v; break;
	case 4: r=t; g=p; b=v; break;
	default: r=v; g=p; b=q; break;
	}
	return ((unsigned long)(r*255.f+.5f) << 16) | ((unsigned long)(g*255.f+.5f) << 8) | (unsigned long)(b*255.f+.5f);
}

void profile_rgb_to_hsv(unsigned long rgb, real *h, real *s, real *v)
{
	real r = ((rgb >> 16)&255)/255.f, g = ((rgb >> 8)&255)/255.f, b = (rgb&255)/255.f;
	real high = MAX(r,MAX(g,b)), low = MIN(r,MIN(g,b)), delta = high-low;
	*v = high; *s = high > 0 ? delta/high : 0; *h = 0;
	if (delta > 0)
	{
		if (high == r) *h = 60.f*((g-b)/delta);
		else if (high == g) *h = 60.f*(2.f+(b-r)/delta);
		else *h = 60.f*(4.f+(r-g)/delta);
		if (*h < 0) *h += 360.f;
	}
}
