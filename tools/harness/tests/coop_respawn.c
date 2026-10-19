#include "harness.h"
#include <string.h>
typedef unsigned short word;
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 128
#define TICKS_PER_SECOND 30
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
#define PIN(a,b,c) MAX((b),MIN((a),(c)))
static long seconds=10;
static long config_integer(char const *name){return seconds;}
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define GET_MESSAGE_TYPE(h) (((h)>>2)&3)
enum {_game_connection_local, _game_connection_network_client, _game_connection_network_server};
enum {_distributed_message_coop_respawn=247};
struct distributed_message_header {word header;byte type,count;long game_time;};
struct player_datum {long unit_index,dead_unit_index,respawn_timer;short local_player_index;boolean quit_out_of_game;
 unsigned short name[13];struct {short machine_index,controller_index;} network_player_data;struct{short deaths;}statistics;};
struct data_iterator {long datum_index;int cursor;};
static struct player_datum people[2];static void *player_data;
static long tick=100, role=2;static boolean coop=TRUE,capable=TRUE,paused,cinematic,playing=TRUE,menu;
static int beeps,go,sends;
static long game_time_get(void){return tick;}
static boolean game_time_get_paused(void){return paused;}
static boolean cinematic_in_progress(void){return cinematic;}
static boolean game_in_progress(void){return playing;}
static boolean main_menu_is_active(void){return menu;}
static long game_connection(void){return role;}
static boolean network_coop_active(void){return coop;}
static boolean network_extensions_allows(long machine,byte type){return capable;}
static struct player_datum *player_try_and_get(long index){return index>=0&&index<2?people+index:NULL;}
static long distributed_player_from_byte(byte p){return p<2?p:NONE;}
static boolean distributed_object_index_valid(long index){return index>=0;}
static void data_iterator_new(struct data_iterator *i,void *d){i->cursor=0;}
static struct player_datum *data_iterator_next(struct data_iterator *i){if(i->cursor==2)return NULL;i->datum_index=i->cursor;return people+i->cursor++;}
static void game_engine_respawn_sound(boolean respawn){if(respawn)go++;else beeps++;}
static void distributed_send_to_machine_reliably(long machine,void *data,byte type,short count,word size){sends++;}
enum {_object_dead_bit=2};
static struct unit_datum {struct {unsigned long damage_flags;} object;} survivor;
static boolean safe=TRUE;
static struct unit_datum *unit_try_and_get(long index){return index==11?&survivor:NULL;}
static boolean players_coop_unit_safe(long index){return safe;}
#include "safety.inc"
#include "under_test.inc"
static void step(int count){while(count--){tick++;coop_respawn_update();}}
static void setup(void){for(int i=0;i<2;i++){people[i].unit_index=NONE;people[i].dead_unit_index=10+i;people[i].local_player_index=i;
 people[i].statistics.deaths=1;people[i].name[0]='A'+i;}coop_respawn_reset();}
static struct respawn_message packet(void){struct respawn_message m={{8,247,1,100},10,300,0,0,{0}};m.identity=respawn_identity(people);return m;}
int main(int argc,char **argv){char const *case_name=argc>1?argv[1]:"";setup();
 CASE("timing") {
  coop_respawn_died(0);coop_respawn_died(1);CHECK(!coop_respawn_ready(0)&&people[0].respawn_timer==300,"initial 10 seconds");
  step(209);CHECK(beeps==0&&!coop_respawn_ready(0),"early beep or spawn");
  step(1);CHECK(beeps==2,"both local players get first beep");
  coop_respawn_update();CHECK(beeps==2,"render/frame duplicate");step(89);CHECK(!coop_respawn_ready(0),"early ready");
  step(1);CHECK(coop_respawn_ready(0)&&beeps==6&&go==0,"countdown not complete");
  people[0].unit_index=20;step(1);CHECK(go==1,"spawn sound absent");step(10);CHECK(go==1,"repeated spawn sound");
 } else CASE("safe") {
  people[1].unit_index=11;coop_respawn_died(0);CHECK(people[0].respawn_timer==90,"safe death not three seconds");
  safe=FALSE;step(90);CHECK(coop_respawn_ready(0),"changed safety lengthened snapshot");
  coop_respawn_died(0);CHECK(people[0].respawn_timer==300,"unsafe death shortened");safe=TRUE;step(1);
  CHECK(people[0].respawn_timer==299,"later safety shortened a death");
  survivor.object.damage_flags=4;CHECK(!players_coop_all_survivors_safe(),"dying survivor safe");survivor.object.damage_flags=0;
  people[1].quit_out_of_game=TRUE;CHECK(!players_coop_all_survivors_safe(),"no survivors was safe");
  people[1].quit_out_of_game=FALSE;people[1].unit_index=12;CHECK(!players_coop_all_survivors_safe(),"stale unit safe");
 } else CASE("pause") {
  coop_respawn_died(0);paused=TRUE;step(500);CHECK(people[0].respawn_timer==300&&!beeps,"pause advanced");
  paused=FALSE;cinematic=TRUE;step(500);CHECK(people[0].respawn_timer==300&&!beeps,"cutscene advanced");
  cinematic=FALSE;step(300);CHECK(coop_respawn_ready(0),"resume timer");
 } else CASE("client") {
  role=1;struct respawn_message m=packet();coop_respawn_handle(NONE,(word*)&m,sizeof(m),TRUE);
  step(210);CHECK(beeps==1&&people[0].respawn_timer==90,"client countdown");
  m.remaining=100;coop_respawn_handle(NONE,(word*)&m,sizeof(m),TRUE);step(1);CHECK(beeps==1,"correction replayed beep");
  step(100);CHECK(people[0].unit_index==NONE&&people[0].respawn_timer==0,"client spawned itself or negative timer");
  people[0].unit_index=20;step(1);CHECK(go==1,"remote respawn sound");
 } else CASE("packets") {
  role=1;struct respawn_message m=packet();
  for(int size=0;size<sizeof(m);size++)coop_respawn_handle(NONE,(word*)&m,size,TRUE);
  coop_respawn_handle(1,(word*)&m,sizeof(m),TRUE);coop_respawn_handle(NONE,(word*)&m,sizeof(m),FALSE);
  m.remaining=1801;coop_respawn_handle(NONE,(word*)&m,sizeof(m),TRUE);m=packet();m.identity++;
  coop_respawn_handle(NONE,(word*)&m,sizeof(m),TRUE);m=packet();m.corpse=20;coop_respawn_handle(NONE,(word*)&m,sizeof(m),TRUE);
  CHECK(!respawn_clocks[0].active,"malformed/spoofed/other-life timer");
  m=packet();people[0].unit_index=10;coop_respawn_handle(NONE,(word*)&m,sizeof(m),TRUE);step(1);
  CHECK(respawn_clocks[0].active&&!go,"timer preceding death was lost");people[0].unit_index=NONE;step(1);CHECK(people[0].respawn_timer==298,"pending death timer");
 } else CASE("reuse") {
  coop_respawn_died(0);step(250);people[0].name[0]='Z';step(1);CHECK(people[0].respawn_timer==300,"reused identity inherited old timer");
  people[0].quit_out_of_game=TRUE;step(400);CHECK(!respawn_clocks[0].active,"quit player counted");
 } else CASE("legacy") {
  coop=FALSE;coop_respawn_died(0);step(400);CHECK(!respawn_clocks[0].active&&!beeps,"multiplayer affected");
  coop=TRUE;role=1;capable=FALSE;coop_respawn_died(0);step(400);CHECK(!respawn_clocks[0].active&&!beeps,"upstream host timer guessed");
 } else return 2;return 0;
}
