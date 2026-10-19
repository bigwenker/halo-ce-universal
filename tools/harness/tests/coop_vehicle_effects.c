#include "harness.h"
#include <string.h>
typedef unsigned short word;
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
#define TICKS_PER_SECOND 30
#define GET_MESSAGE_TYPE(h) (((h)>>2)&3)
#define TAG_BLOCK_GET_ELEMENT(b,i,t) ((t*)(b)->address+(i))
enum {_game_connection_local,_game_connection_network_client,_game_connection_network_server};
enum {_object_mask_vehicle=2,_object_dead_bit=2,_distributed_message_coop_vehicle_effect=248,_distributed_to_clients_reliably=1};
struct distributed_message_header {word header;byte type,count;long game_time;};
struct object_datum {long definition_index;struct {unsigned long damage_flags;}object;};
struct game_globals_player_information {struct {long index;}coop_respawn_effect;};
struct game_globals {struct {long count;void *address;}player_information;};
static struct object_datum vehicle={20,{0}};static struct game_globals_player_information information={{30}};
static struct game_globals globals={{1,&information}};
static int role=2,visible=1,coop=1,capable=1,playing=1,menu,effects,sends;static long tick=10,handle=0x10001;
static long game_connection(void){return role;}
static long game_time_get(void){return tick;}
static boolean network_coop_active(void){return coop;}
static boolean game_in_progress(void){return playing;}
static boolean main_menu_is_active(void){return menu;}
static boolean network_extensions_allows(long machine,byte type){return capable;}
static struct object_datum *object_try_and_get_and_verify_type(long index,long mask){return visible&&index==handle?&vehicle:NULL;}
static struct game_globals *scenario_get_game_globals(void){return &globals;}
static boolean distributed_tag_of_group(long index,long group){return (index==20&&group=='vehi')||(index==30&&group=='effe');}
static boolean distributed_object_index_valid(long index){return index!=NONE&&(index>>16)!=0;}
static long effect_new_from_object(long tag,long owner,long object,short local,real a,real b,void *color,void *field){CHECK(tag==30&&owner==handle&&object==handle,"wrong respawn effect");effects++;return 1;}
static void distributed_send(void *data,byte type,short count,word size,short destination){CHECK(type==248&&count==1&&destination==1,"wrong relay");sends++;}
#include "under_test.inc"
static struct vehicle_effect_message packet(void){struct vehicle_effect_message m={{8,248,1,10},0x10001,20};return m;}
int main(int argc,char **argv){char const *case_name=argc>1?argv[1]:"";struct vehicle_effect_message m=packet();
 CASE("host") {
  coop_vehicle_respawn_effect(handle);CHECK(effects==1&&sends==1,"host effect/relay");
  role=1;coop_vehicle_respawn_effect(handle);coop=0;role=2;coop_vehicle_respawn_effect(handle);CHECK(effects==1&&sends==1,"client/noncoop spawned effect");
 } else CASE("pending") {
  role=1;visible=0;coop_vehicle_effects_handle(NONE,(word*)&m,sizeof(m),TRUE);coop_vehicle_effects_update();CHECK(!effects,"effect before object");
  visible=1;coop_vehicle_effects_update();coop_vehicle_effects_update();CHECK(effects==1,"missing/repeated effect");
  coop_vehicle_effects_handle(NONE,(word*)&m,sizeof(m),TRUE);coop_vehicle_effects_update();CHECK(effects==1,"duplicate notice");
 } else CASE("invalid") {
  role=1;for(int n=0;n<sizeof(m);n++)coop_vehicle_effects_handle(NONE,(word*)&m,n,TRUE);
  coop_vehicle_effects_handle(1,(word*)&m,sizeof(m),TRUE);coop_vehicle_effects_handle(NONE,(word*)&m,sizeof(m),FALSE);
  m.definition=99;coop_vehicle_effects_handle(NONE,(word*)&m,sizeof(m),TRUE);m=packet();capable=0;coop_vehicle_effects_handle(NONE,(word*)&m,sizeof(m),TRUE);
  coop_vehicle_effects_update();CHECK(!effects,"untrusted effect");
 } else CASE("expired") {
  role=1;visible=0;coop_vehicle_effects_handle(NONE,(word*)&m,sizeof(m),TRUE);tick+=91;visible=1;coop_vehicle_effects_update();CHECK(!effects,"expired event played");
  coop_vehicle_effects_reset();tick=10;coop_vehicle_effects_handle(NONE,(word*)&m,sizeof(m),TRUE);handle=0x20001;coop_vehicle_effects_update();CHECK(!effects,"reused object slot played");
 } else return 2;return 0;}
