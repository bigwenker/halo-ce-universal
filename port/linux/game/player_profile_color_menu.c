/* A conventional, palette-independent GUI tint. Source pixels come from the
   player's own ui.map; no extracted game artwork is shipped in the fork. */
#include "cseries.h"
#include "player_profile_rgb.h"
#include "player_profile_color_menu.h"
#include "custom_models.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmaps_sampling.h"
#include "cache/texture_cache.h"
#include "interface/ui_widget.h"
#include "input/input.h"
#include "saved games/player_profile.h"
#include "tag_files/tag_groups.h"
#include "halo_ui_pointer.h"
#include <string.h>
#include <xtl.h>

enum { PORTRAIT_W=128, PORTRAIT_H=256 };
static struct
{
	boolean active, portrait_valid, sources_ready, reset;
	short portrait_palette, stock_palette;
	long portrait_profile;
	unsigned long portrait_rgb;
	real h,s,v;
	short drag;
	rectangle2d square_bounds, hue_bounds;
	struct bitmap_data *square, *hue, *preview, *portrait, *stock;
	unsigned long preview_rgb, shown_rgb;
	real square_h, square_s, square_v, hue_h;
	pixel32 white[PORTRAIT_W*PORTRAIT_H], black[PORTRAIT_W*PORTRAIT_H];
} picker;

void profile_color_menu_invalidate(void) { picker.portrait_profile=NONE; }
void profile_color_menu_portrait(long profile)
{
	struct player_profile saved;
	if (profile == picker.portrait_profile) return;
	picker.portrait_profile=profile;
	picker.portrait_valid=profile_rgb_read(profile,&picker.portrait_rgb);
	picker.portrait_palette=player_profile_get(profile,&saved) ? saved.primary_color_index : 0;
}

void profile_color_menu_begin(long profile, short palette)
{
	unsigned long rgb;
	real_rgb_color color;
    if (!profile_rgb_read(profile,&rgb) && !custom_models_profile_default_rgb(profile,&rgb))
	{
		color=player_profile_get_rgb_color(palette);
		rgb=((unsigned long)(color.red*255.f+.5f)<<16)|((unsigned long)(color.green*255.f+.5f)<<8)|(unsigned long)(color.blue*255.f+.5f);
	}
	profile_rgb_to_hsv(rgb,&picker.h,&picker.s,&picker.v);
	picker.active=TRUE; picker.drag=0; picker.reset=FALSE;
}
void profile_color_menu_cancel(void) { picker.active=FALSE; picker.drag=0; }
void profile_color_menu_reset(long profile, short palette)
{
	profile_color_menu_begin(profile,palette);
	picker.reset=TRUE;
}
boolean profile_color_menu_is_reset(void) { return picker.reset; }
unsigned long profile_color_menu_rgb(void) { return profile_rgb_from_hsv(picker.h,picker.s,picker.v); }
void profile_color_menu_values(real *h, real *s, real *v) { *h=picker.h; *s=picker.s; *v=picker.v; }
void profile_color_menu_adjust(char const *name, short direction)
{
	if (!picker.active) return;
	if (!strcmp(name,"rgb_hue_value")) picker.h=PIN(picker.h+direction,0.f,359.f);
	else if (!strcmp(name,"rgb_saturation_value")) picker.s=PIN(picker.s+direction*.01f,0.f,1.f);
	else if (!strcmp(name,"rgb_brightness_value")) picker.v=PIN(picker.v+direction*.01f,0.f,1.f);
	else return;
	picker.reset=FALSE;
}

static boolean inside(rectangle2d const *r, short x, short y)
{
	return r->x1>r->x0 && r->y1>r->y0 && x>=r->x0 && x<r->x1 && y>=r->y0 && y<r->y1;
}
boolean profile_color_menu_pointer(struct halo_ui_pointer const *pointer)
{
	short x=pointer->x, y=pointer->y;
	boolean clicked=pointer->left_clicks || pointer->downs;
	if (!picker.active) return FALSE;
	if (clicked)
	{
		x=pointer->left_clicks ? pointer->click_x : pointer->down_x;
		y=pointer->left_clicks ? pointer->click_y : pointer->down_y;
		picker.drag=inside(&picker.square_bounds,x,y) ? 1 : inside(&picker.hue_bounds,x,y) ? 2 : 0;
	}
	if (!clicked && !pointer->held) { picker.drag=0; return FALSE; }
	if (picker.drag==1)
	{
		picker.s=PIN((real)(x-picker.square_bounds.x0)/MAX(1,picker.square_bounds.x1-picker.square_bounds.x0-1),0.f,1.f);
		picker.v=1.f-PIN((real)(y-picker.square_bounds.y0)/MAX(1,picker.square_bounds.y1-picker.square_bounds.y0-1),0.f,1.f);
	}
	else if (picker.drag==2)
		picker.h=359.f*PIN((real)(y-picker.hue_bounds.y0)/MAX(1,picker.hue_bounds.y1-picker.hue_bounds.y0-1),0.f,1.f);
	if (picker.drag) picker.reset=FALSE;
	return picker.drag!=0;
}

static boolean portrait_read(struct bitmap_data *bitmap, pixel32 *pixels, boolean cropped)
{
	IDirect3DTexture8 *texture;
	D3DLOCKED_RECT lock;
	struct bitmap_data copy;
	real_point2d point;
	short x,y;
	if (!bitmap || bitmap->width!=PORTRAIT_W || bitmap->height!=PORTRAIT_H || bitmap->type!=0) return FALSE;
	texture=_texture_cache_bitmap_get_hardware_format(bitmap,TRUE,TRUE);
	if (!texture || IDirect3DTexture8_LockRect(texture,0,&lock,NULL,0)<0 || !lock.pBits) return FALSE;
	copy=*bitmap; copy.base_address=lock.pBits; copy.mipmap_count=0;
	for(y=0;y<PORTRAIT_H;y++) for(x=0;x<PORTRAIT_W;x++)
	{
		point.x=((cropped ? 1+x*65/PORTRAIT_W : x)+.5f)/PORTRAIT_W;
		point.y=((cropped ? 1+y*136/PORTRAIT_H : y)+.5f)/PORTRAIT_H;
		pixels[y*PORTRAIT_W+x]=bitmap_2d_get_pixel(&copy,&point,1.f);
	}
	IDirect3DTexture8_UnlockRect(texture,0);
	return TRUE;
}

static boolean portrait_sources(void)
{
	long tag;
	if (picker.sources_ready) return TRUE;
	tag=tag_loaded('bitm',"ui\\shell\\bitmaps\\colors_sm");
	if (tag==NONE || !portrait_read(bitmap_group_get_bitmap_from_sequence(tag,0,0),picker.white,FALSE) ||
		!portrait_read(bitmap_group_get_bitmap_from_sequence(tag,0,1),picker.black,FALSE)) return FALSE;
	picker.sources_ready=TRUE;
	return TRUE;
}

static struct bitmap_data *portrait_make(struct bitmap_data *bitmap, unsigned long rgb)
{
	short x,y,c;
	if (!portrait_sources()) return NULL;
	if (!bitmap) bitmap=bitmap_2d_new(PORTRAIT_W,PORTRAIT_H,0,11);
	if (!bitmap || !bitmap->base_address) return bitmap;
	for(y=0;y<PORTRAIT_H;y++) for(x=0;x<PORTRAIT_W;x++)
	{
		short sx=1+x*65/PORTRAIT_W, sy=1+y*136/PORTRAIT_H;
		pixel32 white=picker.white[sy*PORTRAIT_W+sx], black=picker.black[sy*PORTRAIT_W+sx];
		pixel32 result=white&0xFF000000UL;
		short delta[3], strength=0;
		real confidence;
		for(c=0;c<3;c++) { delta[c]=MAX(0,(short)((white>>(c*8))&255)-(short)((black>>(c*8))&255)); strength=MAX(strength,delta[c]); }
		confidence=PIN((strength-10)/41.f,0.f,1.f);
		if (sx>=23 && sx<=34 && sy>=14 && sy<=24) confidence=0;
		for(c=0;c<3;c++)
		{
			real value=((white>>(c*8))&255)-delta[c]*confidence*(1.f-((rgb>>(c*8))&255)/255.f);
			result|=(pixel32)PIN((long)(value+.5f),0,255)<<(c*8);
		}
		((pixel32 *)bitmap->base_address)[y*PORTRAIT_W+x]=result;
	}
	bitmap_rebuild(bitmap);
	return bitmap;
}

static struct bitmap_data *profile_portrait(void)
{
	if (!picker.portrait_valid) return NULL;
	if (!picker.portrait || picker.shown_rgb!=picker.portrait_rgb)
	{
		picker.portrait=portrait_make(picker.portrait,picker.portrait_rgb);
		picker.shown_rgb=picker.portrait_rgb;
	}
	return picker.portrait;
}

static struct bitmap_data *profile_stock_portrait(void)
{
	long tag;
	struct bitmap_data *source;
	if (picker.stock && picker.stock_palette==picker.portrait_palette) return picker.stock;
	tag=tag_loaded('bitm',"ui\\shell\\bitmaps\\colors_sm");
	if (tag==NONE) return NULL;
	source=bitmap_group_get_bitmap_from_sequence(tag,0,picker.portrait_palette);
	if (!picker.stock) picker.stock=bitmap_2d_new(PORTRAIT_W,PORTRAIT_H,0,11);
	if (!picker.stock) return NULL;
	if (!picker.stock->base_address || !portrait_read(source,(pixel32 *)picker.stock->base_address,TRUE))
	{
		bitmap_delete(picker.stock); picker.stock=NULL;
		return NULL;
	}
	bitmap_rebuild(picker.stock); picker.stock_palette=picker.portrait_palette;
	return picker.stock;
}

struct bitmap_data *profile_color_menu_bitmap(char const *name, struct bitmap_data *original)
{
	short x,y;
    if (!strcmp(name,"custom_model_preview"))
    {
        struct bitmap_data *portrait=profile_portrait();
        if (!portrait) portrait=profile_stock_portrait();
        return custom_models_preview_bitmap(name,portrait ? portrait : original);
    }
	if (!strcmp(name,"qtr_screen_profile_color_pic") && picker.portrait_valid)
	{
		struct bitmap_data *portrait=profile_portrait();
		return portrait ? portrait : original;
	}
	if (!picker.active) return original;
	if (!strcmp(name,"rgb_preview"))
	{
		unsigned long rgb=profile_color_menu_rgb();
		if (!picker.preview || picker.preview_rgb!=rgb)
		{
			picker.preview=portrait_make(picker.preview,rgb); picker.preview_rgb=rgb;
		}
		return picker.preview ? picker.preview : original;
	}
	if (!strcmp(name,"rgb_square"))
	{
		if (!picker.square) { picker.square=bitmap_2d_new(128,128,0,11); picker.square_h=-1; }
		if (!picker.square) return original;
		if (picker.square_h!=picker.h || picker.square_s!=picker.s || picker.square_v!=picker.v)
		{
			for(y=0;y<128;y++) for(x=0;x<128;x++)
			{
				short dx=x-(short)(picker.s*127.f), dy=y-(short)((1.f-picker.v)*127.f);
				pixel32 color=0xFF000000UL|profile_rgb_from_hsv(picker.h,x/127.f,1.f-y/127.f);
				if (dx*dx+dy*dy>=9 && dx*dx+dy*dy<=18) color=0xFFFFFFFFUL;
				((pixel32 *)picker.square->base_address)[y*128+x]=color;
			}
			bitmap_rebuild(picker.square); picker.square_h=picker.h; picker.square_s=picker.s; picker.square_v=picker.v;
		}
		return picker.square;
	}
	if (!strcmp(name,"rgb_hue_strip"))
	{
		if (!picker.hue) { picker.hue=bitmap_2d_new(16,256,0,11); picker.hue_h=-1; }
		if (!picker.hue) return original;
		if (picker.hue_h!=picker.h)
		{
			for(y=0;y<256;y++) for(x=0;x<16;x++)
				((pixel32 *)picker.hue->base_address)[y*16+x]=
					0xFF000000UL|((y==(short)(picker.h*255.f/359.f)) ? 0xFFFFFFUL : profile_rgb_from_hsv(y*359.f/255.f,1,1));
			bitmap_rebuild(picker.hue); picker.hue_h=picker.h;
		}
		return picker.hue;
	}
	return original;
}

boolean profile_color_menu_bounds(char const *name, rectangle2d *bounds)
{
	if (!strcmp(name,"rgb_square")) { picker.square_bounds=*bounds; return TRUE; }
	if (!strcmp(name,"rgb_hue_strip")) { picker.hue_bounds=*bounds; return TRUE; }
	if (!strcmp(name,"rgb_preview") || !strcmp(name,"custom_model_preview")) return TRUE;
	if (!strcmp(name,"qtr_screen_profile_color_pic") && picker.portrait_valid)
	{
		bounds->x1=bounds->x0+65; bounds->y1=bounds->y0+136; return TRUE;
	}
	return FALSE;
}

void profile_color_menu_forget(void)
{
	custom_models_preview_forget();
	if (picker.stock) bitmap_delete(picker.stock);
	if (picker.square) bitmap_delete(picker.square);
	if (picker.hue) bitmap_delete(picker.hue);
	if (picker.preview) bitmap_delete(picker.preview);
	if (picker.portrait) bitmap_delete(picker.portrait);
	memset(&picker,0,sizeof(picker)); picker.portrait_profile=NONE;
}
