#include "harness.h"
typedef unsigned short word;
#define NUMBEROF(a) (sizeof(a)/sizeof(a[0]))
#define GET_MESSAGE_TYPE(h) ((h)>>2&3)
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 128
enum { _game_connection_local,_game_connection_network_client,_game_connection_network_server, RGB_MESSAGE=251,RGB_CHUNK=128 };
struct distributed_message_header { word header; byte type,count; long game_time; };
struct rgb_entry { byte player,present; word reserved; unsigned long identity,rgb; };
struct player_datum { struct { unsigned short name[12]; signed char machine_index,controller_index; } network_player_data; long handle,unit_index; short local_player_index; int quit_out_of_game; } player;
static struct { long handle,unit; unsigned long rgb; int present; } rgb_players[128];
static boolean machine_ready[128],host_ready;static unsigned long sent=9;
static int role=2;
static int game_connection(void){return role;}
static int game_in_progress(void){return 1;}
static int main_menu_is_active(void){return 0;}
static long distributed_player_from_byte(byte b){return b==1?player.handle:NONE;}
static struct player_datum *player_try_and_get(long index){return index==player.handle?&player:NULL;}
typedef struct {real red,green,blue;} real_rgb_color;
#define NUMBER_OF_OBJECT_CHANGE_COLORS 4
struct object_datum {long definition_index;struct {real_rgb_color base_change_colors[4],outgoing_change_colors[4];} object;} unit;
struct object_change_color_definition {int scaled_by,darken_by;} channels[4];
struct object_definition {struct {struct {int count;void *address;} change_colors;} object;} definition;
#define TAG_BLOCK_GET_ELEMENT(block,i,type) ((type *)(block)->address+(i))
static struct object_datum *object_try_and_get(long index){return index==NONE?NULL:&unit;}
static struct object_definition *object_definition_get(long index){(void)index;return &definition;}
static boolean network_profile_rgb_color(long index,real_rgb_color *color){(void)index;*color=(real_rgb_color){.2f,.5f,.8f};return TRUE;}
static int teams,coop;
static boolean game_engine_has_teams(void){return teams;}
static boolean network_coop_active(void){return coop;}
#include "under_test.inc"
static struct { struct distributed_message_header h;struct rgb_entry e; } msg;
static void reset(void){memset(rgb_players,0,sizeof(rgb_players));memset(machine_ready,0,sizeof(machine_ready));host_ready=0;sent=9;}
int main(int argc,char **argv){CHECK(argc==2,"case");
 player.handle=0x10001;player.local_player_index=NONE;player.network_player_data.machine_index=3;player.network_player_data.controller_index=0;player.network_player_data.name[0]='Q';
 msg.h.header=8;msg.h.type=RGB_MESSAGE;msg.h.count=1;msg.e.player=1;msg.e.present=1;msg.e.rgb=0x123456;msg.e.identity=rgb_identity(&player);
 if(!strcmp(argv[1],"authenticated")){
  network_profile_rgb_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(rgb_players[1].rgb==0x123456&&rgb_players[1].handle==player.handle&&machine_ready[3],"authenticated color");
 }else if(!strcmp(argv[1],"sender")){
  network_profile_rgb_handle(4,(word*)&msg,sizeof(msg),TRUE);CHECK(!rgb_players[1].present&&!machine_ready[4],"forged owner");
  network_profile_rgb_handle(3,(word*)&msg,sizeof(msg),FALSE);CHECK(!rgb_players[1].present,"unreliable request");
 }else if(!strcmp(argv[1],"malformed")){
  for(unsigned n=0;n<sizeof(msg);n++)network_profile_rgb_handle(3,(word*)&msg,n,TRUE);CHECK(!rgb_players[1].present,"truncated");
  msg.e.present=2;network_profile_rgb_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!rgb_players[1].present,"invalid presence");msg.e.present=1;
  msg.e.rgb=0xFF123456;network_profile_rgb_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!rgb_players[1].present,"out of gamut bits");msg.e.rgb=1;
  msg.e.reserved=1;network_profile_rgb_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!rgb_players[1].present,"reserved data");msg.e.reserved=0;
  msg.e.player=255;network_profile_rgb_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!rgb_players[1].present,"invalid player");
 }else if(!strcmp(argv[1],"client")){
  role=1;network_profile_rgb_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!rgb_players[1].present,"client accepts host only");
  network_profile_rgb_handle(NONE,(word*)&msg,sizeof(msg),TRUE);CHECK(rgb_players[1].present&&host_ready,"host snapshot");
  reset();player.local_player_index=0;network_profile_rgb_handle(NONE,(word*)&msg,sizeof(msg),TRUE);CHECK(!rgb_players[1].present,"host cannot override own profile");
 }else if(!strcmp(argv[1],"stale")){
  player.network_player_data.name[0]='Z';network_profile_rgb_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!rgb_players[1].present,"recycled player identity");
 }else if(!strcmp(argv[1],"materials")){
  definition.object.change_colors.count=1;definition.object.change_colors.address=channels;channels[0].scaled_by=1;
  rgb_apply(player.handle,&player);CHECK(unit.object.outgoing_change_colors[0].red==0,"shield channel altered");
  for(int i=1;i<4;i++)CHECK(unit.object.outgoing_change_colors[i].red==.2f&&unit.object.base_change_colors[i].blue==.8f,"implicit armor channel missing");
 }else if(!strcmp(argv[1],"teams")){
  teams=1;rgb_apply(player.handle,&player);CHECK(unit.object.base_change_colors[1].red==0,"team color overridden");
  coop=1;rgb_apply(player.handle,&player);CHECK(unit.object.base_change_colors[1].red==.2f,"co-op tint blocked");
 }else CHECK(0,"unknown");return 0;}
