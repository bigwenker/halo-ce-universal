#include "harness.h"
#include "halo_custom_models.h"

#define MAXIMUM_LOCAL_PLAYERS 4
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
typedef struct { real red,green,blue; } real_rgb_color;
static struct halo_custom_model_info models[2];
static struct player_datum { short local_player_index; boolean quit_out_of_game; } player;
static struct { long handle; unsigned long rgb; boolean present; } rgb_players[8];
static struct { unsigned long rgb; boolean present; } local_colors[4];
static char const *selection="space_marine";
static long handle=0x10001;
static int parse_calls,refresh_calls,profile_present=TRUE;
static struct player_datum *player_try_and_get(long index) { return index==handle ? &player : NULL; }
static void rgb_refresh(void) { refresh_calls++; }
static char const *custom_models_player_id(long index) { (void)index;return selection; }
int halo_custom_model_find(char const *id) { return !strcmp(id,"space_marine") ? 1 : 0; }
struct halo_custom_model_info const *halo_custom_model_info(int index) { return &models[index]; }
static boolean custom_models_profile_read(long profile,char id[32])
{
    if(profile==NONE || !profile_present)return FALSE;
    strcpy(id,selection);return TRUE;
}
#include "parse.inc"
static boolean counted_parse(char const *text,unsigned long *rgb)
{ parse_calls++;return profile_rgb_parse(text,rgb); }
#define profile_rgb_parse counted_parse
#include "defaults.inc"
#undef profile_rgb_parse
#include "color.inc"

int main(int argc,char **argv)
{
    real_rgb_color color;
    unsigned long rgb=0x123456;
    CHECK(argc==2,"case");
    models[0].colorable=TRUE;
    models[1].colorable=TRUE;strcpy(models[1].default_rgb,"#6A0001");
    if(!strcmp(argv[1],"local_default"))
    {
        CHECK(network_profile_rgb_color(handle,&color),"missing profile RGB did not use model default");
        CHECK(color.red==106/255.f && color.green==0 && color.blue==1/255.f,"wrong default tint");
        CHECK(refresh_calls==1 && !local_colors[0].present,"default became a saved/advertised profile RGB");
    }
    else if(!strcmp(argv[1],"profile_priority"))
    {
        local_colors[0].present=TRUE;local_colors[0].rgb=0x3380CC;
        CHECK(network_profile_rgb_color(handle,&color) && color.blue==204/255.f,"explicit profile overridden");
        local_colors[0].rgb=0;
        CHECK(network_profile_rgb_color(handle,&color) && color.red==0 && color.blue==0,"explicit black overridden");
        CHECK(!parse_calls,"default evaluated despite explicit profile RGB");
    }
    else if(!strcmp(argv[1],"remote_default"))
    {
        player.local_player_index=NONE;
        CHECK(network_profile_rgb_color(handle,&color) && color.red==106/255.f,"remote absent RGB default");
        rgb_players[1].present=TRUE;rgb_players[1].handle=0x20001;rgb_players[1].rgb=0xFFFFFF;
        CHECK(network_profile_rgb_color(handle,&color) && color.blue==1/255.f,"stale player RGB leaked");
        CHECK(!refresh_calls,"remote color read local profile");
    }
    else if(!strcmp(argv[1],"remote_priority"))
    {
        player.local_player_index=NONE;rgb_players[1].present=TRUE;
        rgb_players[1].handle=handle;rgb_players[1].rgb=0;
        CHECK(network_profile_rgb_color(handle,&color) && color.red==0 && color.blue==0,"remote black overridden");
        CHECK(!parse_calls,"remote explicit RGB evaluated default");
    }
    else if(!strcmp(argv[1],"noncolorable"))
    {
        models[1].colorable=FALSE;
        CHECK(!network_profile_rgb_color(handle,&color),"non-colorable pack used default");
        CHECK(!parse_calls,"non-colorable default evaluated");
    }
    else if(!strcmp(argv[1],"missing_pack"))
    {
        selection="absent";
        CHECK(!network_profile_rgb_color(handle,&color),"missing pack changed Chief's palette");
    }
    else if(!strcmp(argv[1],"invalid_default"))
    {
        char const *bad[]={"","#12345","#gggggg","1234567"};
        for(int i=0;i<4;i++)
        {strcpy(models[1].default_rgb,bad[i]);CHECK(!network_profile_rgb_color(handle,&color),"bad default accepted");}
    }
    else if(!strcmp(argv[1],"black_default"))
    {
        strcpy(models[1].default_rgb,"#000000");
        CHECK(network_profile_rgb_color(handle,&color) && color.red==0 && color.blue==0,"black default lost");
    }
    else if(!strcmp(argv[1],"profile_picker"))
    {
        CHECK(custom_models_profile_default_rgb(5,&rgb) && rgb==0x6A0001,"picker default");
        CHECK(!custom_models_profile_default_rgb(NONE,&rgb),"guest inherited model default");
        profile_present=FALSE;
        CHECK(!custom_models_profile_default_rgb(5,&rgb),"no selection inherited default");
    }
    else if(!strcmp(argv[1],"invalid_player"))
    {
        CHECK(!network_profile_rgb_color(0x20001,&color),"stale player accepted");
        player.quit_out_of_game=TRUE;
        CHECK(!network_profile_rgb_color(handle,&color),"departed player accepted");
        CHECK(!parse_calls,"invalid player evaluated default");
    }
    else CHECK(FALSE,"unknown case");
    return 0;
}
