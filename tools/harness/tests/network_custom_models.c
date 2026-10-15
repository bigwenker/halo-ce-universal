#include "harness.h"
#include <string.h>
typedef unsigned short word;
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
#define GET_MESSAGE_TYPE(h) ((h)>>2&3)
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 128
enum {_game_connection_local,_game_connection_network_client,_game_connection_network_server,MODEL_MESSAGE=252,MODEL_CHUNK=16};
struct distributed_message_header {word header;byte type,count;long game_time;};
struct model_entry {byte player;byte reserved[3];unsigned long identity;char id[32];};
static struct {long handle;char id[32];} choices[128];
struct player_datum {struct {unsigned short name[12];signed char machine_index,controller_index;}network_player_data;long handle;short local_player_index;int quit_out_of_game;} player;
static int role=2;
static void custom_models_player_changed(long index){(void)index;}
static int game_connection(void){return role;}
static int game_in_progress(void){return 1;}
static int main_menu_is_active(void){return 0;}
static long distributed_player_from_byte(byte b){return b==1?player.handle:NONE;}
static struct player_datum *player_try_and_get(long i){return i==player.handle?&player:NULL;}
#include "id.inc"
#include "under_test.inc"
static struct {struct distributed_message_header h;struct model_entry e;} msg;
int main(int argc,char **argv)
{
 CHECK(argc==2,"case");player.handle=0x10001;player.local_player_index=NONE;player.network_player_data.machine_index=3;player.network_player_data.name[0]='Q';
 msg.h.header=8;msg.h.type=252;msg.h.count=1;msg.e.player=1;msg.e.identity=identity(&player);strcpy(msg.e.id,"space_marine");
 if(!strcmp(argv[1],"valid")){custom_models_network_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!strcmp(choices[1].id,"space_marine"),"authenticated selection");}
 else if(!strcmp(argv[1],"sender")){custom_models_network_handle(4,(word*)&msg,sizeof(msg),TRUE);CHECK(!choices[1].id[0],"forged owner");}
 else if(!strcmp(argv[1],"reliable")){custom_models_network_handle(3,(word*)&msg,sizeof(msg),FALSE);CHECK(!choices[1].id[0],"datagram selection");}
 else if(!strcmp(argv[1],"truncated")){for(unsigned n=0;n<sizeof(msg);n++)custom_models_network_handle(3,(word*)&msg,n,TRUE);CHECK(!choices[1].id[0],"truncated selection");}
 else if(!strcmp(argv[1],"path")){strcpy(msg.e.id,"../space_marine");custom_models_network_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!choices[1].id[0],"path escape");}
 else if(!strcmp(argv[1],"unterminated")){memset(msg.e.id,'a',32);custom_models_network_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!choices[1].id[0],"unterminated ID");}
 else if(!strcmp(argv[1],"stale")){player.network_player_data.name[0]='Z';custom_models_network_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!choices[1].id[0],"recycled player");}
 else if(!strcmp(argv[1],"client")){role=1;custom_models_network_handle(3,(word*)&msg,sizeof(msg),TRUE);CHECK(!choices[1].id[0],"nonhost snapshot");custom_models_network_handle(NONE,(word*)&msg,sizeof(msg),TRUE);CHECK(!strcmp(choices[1].id,"space_marine"),"host snapshot");}
 else if(!strcmp(argv[1],"local")){role=1;player.local_player_index=0;custom_models_network_handle(NONE,(word*)&msg,sizeof(msg),TRUE);CHECK(!choices[1].id[0],"remote override of local selection");}
 else CHECK(0,"unknown case");return 0;
}
