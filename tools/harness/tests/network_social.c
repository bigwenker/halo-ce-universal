#include "harness.h"
#include <stdarg.h>
#include <wchar.h>
#include <math.h>
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 128
#define HALO_PORT_MAXIMUM_OBJECTS_PER_MAP 8192
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef unsigned short word;
typedef struct {real alpha,red,green,blue;} real_argb_color;
#define SET_FLAG(v,b,on) ((v)=((v)&~(1u<<(b)))|((on)?1u<<(b):0))
enum { _game_connection_local, _game_connection_network_client, _game_connection_network_server };
enum { _distributed_message_social_request=96,_distributed_message_social_line,_distributed_message_social_scores };
enum { _distributed_to_clients, _distributed_to_clients_reliably };
enum { _object_dead_bit, _object_outside_of_map_bit, _damage_no_statistics_bit, _damage_headshot_bit, _damage_chain_reaction_bit };
struct distributed_message_header { word header; byte type,count; long game_time; };
#define GET_MESSAGE_TYPE(h) (((h)>>2)&3)
static void build_message_header(word *h,word size,byte type,byte flags){*h=(size<<4)|(type<<2)|flags;}
struct player_datum { wchar_t name[13]; boolean quit_out_of_game; long unit_index; short team_index; struct {short machine_index;} network_player_data; };
struct object_datum {long definition_index; struct {unsigned long flags,damage_flags;short owner_team_index;} object;};
struct unit_datum {long definition_index; struct {unsigned long flags,damage_flags;short owner_team_index;} object; struct {long player_index;}unit;};
struct damage_data {long definition_index,owner_player_index,owner_object_index;unsigned long flags;};
struct damage_definition {short category;};
static struct damage_effect_definition {struct damage_definition damage;} damage_def;
static struct player_datum players[128];static long handles[128];
static struct unit_datum units[20];static long unit_handles[20];
static int role=2;static boolean coop=TRUE,playing=TRUE,menu=FALSE;
static long percent=100;static unsigned long clock_ms=1000;static long tick=30;
static int killed,shown,sent,chatted,warned;static long last_killed;
static char shown_text[256],chat_text[256];static byte wire[2048];static word wire_size;
static void *player_data=(void*)1;
struct data_iterator {long datum_index;int next;};
static void data_iterator_new(struct data_iterator *it,void *array){it->next=0;}
static void *data_iterator_next(struct data_iterator *it){while(it->next<128){int n=it->next++;if(handles[n]!=NONE){it->datum_index=handles[n];return &players[n];}}return NULL;}
static struct player_datum *player_try_and_get(long index){int n=DATUM_INDEX_TO_ABSOLUTE_INDEX(index);return index!=NONE&&n<128&&handles[n]==index?&players[n]:NULL;}
static struct unit_datum *unit_try_and_get(long index){int n=DATUM_INDEX_TO_ABSOLUTE_INDEX(index);return index!=NONE&&n<20&&unit_handles[n]==index?&units[n]:NULL;}
static struct object_datum *object_try_and_get(long index){return (struct object_datum*)unit_try_and_get(index);}
static long distributed_player_from_byte(byte n){return n<128?handles[n]:NONE;}
static long local_player_get_player_index(int n){return handles[n];}
static int game_connection(void){return role;}
static boolean network_coop_active(void){return coop;}
static boolean game_in_progress(void){return playing;}
static boolean main_menu_is_active(void){return menu;}
static boolean console_is_active(void){return FALSE;}
static long game_time_get(void){return tick;}
static unsigned long system_milliseconds(void){return clock_ms;}
static long config_integer(char const *name){return percent;}
static boolean game_team_is_enemy(short a,short b){return a>=0&&b>=0&&a!=b;}
static char const *tags[]={"characters\\cyborg\\cyborg","characters\\grunt\\grunt","characters\\elite\\elite","characters\\hunter\\hunter","characters\\floodcombat elite\\floodcombat elite","vehicles\\wraith\\wraith","weapons\\plasma grenade\\explosion","weapons\\frag grenade\\explosion","globals\\falling","globals\\maximum distance"};
static char const *tag_get_name(long index){return tags[index];}
static struct damage_effect_definition *damage_effect_definition_get(long index){return &damage_def;}
static void unit_kill(long index){killed++;last_killed=index;}
enum {_chat_channel_everyone};
static void network_chat_say(short channel,char const *text){chatted++;snprintf(chat_text,sizeof(chat_text),"%s",text);}
static void console_warning(char const *text,...){warned++;}
static void terminal_printf(real_argb_color const *color,char const *format,...){va_list args;va_start(args,format);vsnprintf(shown_text,sizeof(shown_text),format,args);va_end(args);shown++;}
static void telnet_console_print(char const *text){snprintf(shown_text,sizeof(shown_text),"%s",text);shown++;}
static void distributed_send(void *message,byte type,byte count,word size,int direction){memcpy(wire,message,size);wire_size=size;sent++;}
boolean network_distributed_client_send_reliably(void *message,word size){memcpy(wire,message,size);wire_size=size;sent++;return TRUE;}
static boolean network_extensions_allows(long machine, byte type) { (void)machine; (void)type; return TRUE; }
/* Retired wire layout, only for proving old chat packets are ignored. */
struct social_request {byte player,operation;char text[200];};
#include "under_test.inc"
static void setup(void){
 for(int i=0;i<128;i++)handles[i]=NONE;for(int i=0;i<20;i++)unit_handles[i]=NONE;
 memset(players,0,sizeof(players));memset(units,0,sizeof(units));
 for(int i=0;i<2;i++){handles[i]=0x10000+i;unit_handles[i]=0x20000+i;players[i].unit_index=unit_handles[i];players[i].team_index=1;players[i].network_player_data.machine_index=i;units[i].unit.player_index=handles[i];units[i].object.owner_team_index=1;}
 wcscpy(players[0].name,L"Host");wcscpy(players[1].name,L"Quixote");
 unit_handles[2]=0x20002;units[2].unit.player_index=NONE;units[2].object.owner_team_index=2;units[2].definition_index=1;
 network_social_reset();shown=sent=chatted=killed=warned=0;
}
static void request(byte player,byte operation,char const *text,long machine){struct {struct distributed_message_header h;struct social_request r;}m={0};social_header(&m.h,96,1);m.r.player=player;m.r.operation=operation;snprintf(m.r.text,sizeof(m.r.text),"%s",text);network_social_handle_message(machine,(word*)&m,sizeof(m));}
int main(int argc,char **argv){CHECK(argc==2,"case");setup();char const *case_name=argv[1];
 CASE("chat") {boolean success=FALSE;
  CHECK(network_social_console_command("say Hey guys",&success)&&success&&chatted==1&&!strcmp(chat_text,"Hey guys"),"say must use upstream chat");
  CHECK(sent==0&&shown==0,"old chat transport/display still active");
  CHECK(!network_social_console_command("saywhat",&success),"say prefix collision");
  role=1;CHECK(network_social_chat_send("/kill")&&chatted==2&&killed==0,"console say must stay text");
  role=0;CHECK(!network_social_chat_send("offline")&&chatted==2,"offline chat");
  role=2;menu=TRUE;CHECK(!network_social_chat_send("menu")&&chatted==2,"menu chat");
 }
 else CASE("retired") {
  for(int connection=1;connection<=2;connection++)for(int kind=96;kind<=97;kind++)for(int machine=-1;machine<=1;machine++){
   role=connection;request(1,0,"/kill",machine);
   for(int size=0;size<216;size++){byte bad[216]={0};struct distributed_message_header *h=(void*)bad;social_header(h,kind,1);network_social_handle_message(machine,(word*)bad,size);}
  }
  CHECK(chatted==0&&shown==0&&sent==0&&killed==0,"retired chat bypassed upstream policy");
 }
 else CASE("score") {struct damage_data d={6,handles[0],unit_handles[0],0};
  for(int tag=1;tag<=4;tag++)for(int head=0;head<2;head++){network_social_reverted();long before=network_social_score(handles[0]);units[2].definition_index=tag;d.flags=head?FLAG(_damage_headshot_bit):0;network_social_note_death(unit_handles[2],&d);long points=1+head+(tag==2||tag==3);CHECK(network_social_score(handles[0])==before+points,"species/headshot points tag%d head%d",tag,head);network_social_note_death(unit_handles[2],&d);CHECK(network_social_score(handles[0])==before+points,"duplicate death");}
  network_social_reverted();units[2].object.owner_team_index=1;long before=network_social_score(handles[0]);network_social_note_death(unit_handles[2],&d);CHECK(network_social_score(handles[0])==before,"ally awarded points");
  handles[0]=0x30000;CHECK(network_social_score(handles[0])==0,"reused player inherited score");
 }
 else CASE("chain") {struct damage_data d={6,NONE,NONE,FLAG(_damage_chain_reaction_bit)};for(int n=25;n<=100;n+=25){percent=n;CHECK(fabsf(network_social_chain_damage_scale(&d,unit_handles[0])-n*.01f)<1e-6,"player scale");CHECK(network_social_chain_damage_scale(&d,unit_handles[2])==1,"AI scaled");}percent=25;d.flags=0;CHECK(network_social_chain_damage_scale(&d,unit_handles[0])==1,"thrown grenade scaled");d.flags=FLAG(_damage_chain_reaction_bit);coop=FALSE;CHECK(network_social_chain_damage_scale(&d,unit_handles[0])==1,"competitive affected");coop=TRUE;role=1;CHECK(network_social_chain_damage_scale(&d,unit_handles[0])==1,"client config affected authoritative replay");role=2;percent=-1;CHECK(network_social_chain_damage_scale(&d,unit_handles[0])==1,"invalid config");}
 else CASE("snapshots") {network_social_score(handles[1]);social_players[1].score=37;network_social_update();CHECK(sent==1,"no snapshot");role=1;social_players[1].score=0;network_social_handle_message(NONE,(word*)wire,wire_size);CHECK(network_social_score(handles[1])==37,"client score missing");social_players[1].score=0;network_social_handle_message(0,(word*)wire,wire_size);CHECK(network_social_score(handles[1])==0,"score accepted from client");players[1].name[0]='Z';network_social_handle_message(NONE,(word*)wire,wire_size);CHECK(network_social_score(handles[1])==0,"stale identity applied");}
 else return 2;return 0;
}
