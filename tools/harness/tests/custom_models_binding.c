#include "harness.h"
#include <string.h>
enum {_object_type_biped=0};
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
struct object_datum {struct {short type;long owner_player_index;}object;} unit;
struct player_datum {long unit_index;} player;
static struct {long handle,tag;} objects[32];
static long stock_model=5,chosen=9,handle=0x10001;
static struct object_datum *object_try_and_get(long i){return i==handle?&unit:NULL;}
static struct player_datum *player_try_and_get(long i){return i==7?&player:NULL;}
static long model_for_player(long i){return i==7?chosen:NONE;}
#include "binding.inc"
int main(int argc,char **argv)
{
 CHECK(argc==2,"case");unit.object.type=0;unit.object.owner_player_index=7;player.unit_index=handle;
 if(!strcmp(argv[1],"player")){custom_models_object_created(handle);CHECK(custom_models_render_model(handle,5)==9,"player cosmetic");}
 else if(!strcmp(argv[1],"missing")){chosen=NONE;CHECK(custom_models_render_model(handle,5)==5,"missing pack fallback");}
 else if(!strcmp(argv[1],"npc")){unit.object.owner_player_index=NONE;CHECK(custom_models_render_model(handle,5)==5,"NPC replaced");}
 else if(!strcmp(argv[1],"weapon")){unit.object.type=2;CHECK(custom_models_render_model(handle,5)==5,"weapon replaced");}
 else if(!strcmp(argv[1],"other_model")){CHECK(custom_models_render_model(handle,6)==6,"non-Chief model replaced");}
 else if(!strcmp(argv[1],"corpse")){custom_models_object_created(handle);unit.object.owner_player_index=NONE;CHECK(custom_models_render_model(handle,5)==9,"corpse lost cosmetic");}
 else if(!strcmp(argv[1],"reused")){custom_models_object_created(handle);handle=0x20001;unit.object.owner_player_index=NONE;CHECK(custom_models_render_model(handle,5)==5,"recycled object inherited cosmetic");}
 else CHECK(0,"unknown case");return 0;
}
