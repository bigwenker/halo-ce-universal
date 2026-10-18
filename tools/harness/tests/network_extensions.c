#include "harness.h"
#include <string.h>
typedef unsigned short word;
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 128
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
enum { _game_connection_local, _game_connection_network_client, _game_connection_network_server };
enum { _distributed_to_clients, _distributed_to_clients_reliably, _distributed_to_host, _distributed_to_host_reliably };
struct distributed_message_header { word header; byte type, count; long game_time; };
#define GET_MESSAGE_TYPE(h) (((h)>>2)&3)
#include "messages.inc"
#include "api.inc"
static int role=2, playing=1, menu, sends, policies;
static unsigned long now=1000;
static byte wire[16][256];
static long recipients[16];
static word sizes[16];
static struct {long sent;} distributed_statistics;
static int game_connection(void){return role;}
static boolean game_in_progress(void){return playing;}
static boolean main_menu_is_active(void){return menu;}
static long game_time_get(void){return 30;}
static unsigned long system_milliseconds(void){return now;}
static void build_message_header(word *h,word size,byte type,byte flags){*h=(size<<4)|(type<<2)|flags;}
static void record(long peer,void const *data,word size){CHECK(sends<16,"send overflow");memcpy(wire[sends],data,size);recipients[sends]=peer;sizes[sends++]=size;}
boolean network_distributed_client_send_reliably(void *data,word size){record(NONE,data,size);return TRUE;}
static void network_distributed_server_send_to_machine_reliably(long peer,void *data,word size){record(peer,data,size);}
static void network_distributed_server_send_to_all_reliably(void *data,word size){record(1,data,size);record(2,data,size);}
short network_distributed_server_machines(long *out,short maximum){out[0]=1;out[1]=2;return 2;}
static short distributed_client_machines(long *out,short maximum){return network_distributed_server_machines(out,maximum);}
static void distributed_batch_add(short peer,void const *data,word size,word entry_size){record(peer,data,size);}
static void network_aim_assist_send_policy(long machine){if(network_extensions_allows(machine,_distributed_message_aim_assist_policy))policies++;}
static int spawn_ready, noclip_ready;
static void network_spawn_machine_ready(long machine){(void)machine;spawn_ready++;}
static void network_noclip_machine_ready(long machine){(void)machine;noclip_ready++;}
void distributed_send_to_machine_reliably(long,void*,byte,short,word);
#define HOST_SENDER 128
#define csmemcpy memcpy
#include "under_test.inc"
#include "transport.inc"
static struct extension_message hello(void){struct extension_message m={{8,_distributed_message_fork_capabilities,1,30},EXT_MAGIC,EXT_REVISION,EXT_ALL};return m;}
static void negotiate(word features){struct extension_message m=hello();m.features=features;role=2;network_extensions_handle(1,(word*)&m,sizeof(m),TRUE);}
int main(int argc,char **argv){CHECK(argc==2,"case");char const *case_name=argv[1];network_extensions_reset();
 CASE("legacy") {
  role=1;network_extensions_select_host(FALSE);network_extensions_update();now+=10000;network_extensions_update();CHECK(!sends,"probed upstream host");
  CHECK(!network_extensions_allows(NONE,_distributed_message_social_request),"upstream social enabled");
  role=2;CHECK(!network_extensions_allows(1,250),"upstream spray enabled");CHECK(network_extensions_allows(1,_distributed_message_map_share_offer),"core map sharing blocked");
 } else CASE("negotiation") {
  role=1;network_extensions_select_host(TRUE);network_extensions_update();CHECK(sends==1&&sizes[0]==16,"hello layout");
  CHECK(!network_extensions_allows(NONE,250),"enabled before ack");role=2;network_extensions_handle(1,(word*)wire[0],sizes[0],TRUE);
  CHECK(sends==2&&policies==1&&network_extensions_allows(1,250),"host negotiation");CHECK(!network_extensions_allows(2,250),"enabled upstream peer");
  role=1;network_extensions_handle(NONE,(word*)wire[1],sizes[1],TRUE);CHECK(network_extensions_allows(NONE,250),"client ack");network_extensions_update();CHECK(sends==2,"hello spam");
 } else CASE("partial") {
  negotiate(EXT_SUICIDE);CHECK(network_extensions_allows(1,_distributed_message_suicide_request),"missing selected feature");CHECK(!network_extensions_allows(1,_distributed_message_aim_assist_policy)&&!policies,"unselected feature enabled");
  CHECK(!network_extensions_allows(2,_distributed_message_suicide_request),"other machine enabled");
  CHECK(!spawn_ready&&!noclip_ready,"unselected machine readied");
  network_extensions_machine_joined(1);negotiate(EXT_SPAWN);CHECK(spawn_ready==1&&!noclip_ready,"spawn alone");
  network_extensions_machine_joined(1);negotiate(EXT_NOCLIP);CHECK(spawn_ready==1&&noclip_ready==1&&network_extensions_allows(1,252),"noclip alone");
 } else CASE("hellos") {
  /* (unanswered, a client offers every feature, then those before noclip,
  then those before the spawn menu, in turn: older hosts drop a hello with
  a bit they do not know) */
  struct extension_message m;word offered[4];int n;
  role=1;network_extensions_select_host(TRUE);
  for(n=0;n<4;n++){network_extensions_update();CHECK(sends==n+1,"hello not sent");memcpy(&m,wire[n],sizeof(m));offered[n]=m.features;now+=2000;}
  CHECK(offered[0]==EXT_ALL&&offered[1]==EXT_BEFORE_NOCLIP&&offered[2]==EXT_LEGACY&&offered[3]==EXT_ALL,"hello rotation");
  CHECK(EXT_ALL==127&&EXT_BEFORE_NOCLIP==63&&EXT_LEGACY==31,"feature sets");
  CHECK(network_extensions_feature(251)==EXT_SPAWN&&network_extensions_feature(252)==EXT_NOCLIP,"private message features");
 } else CASE("admission") {
  struct extension_message m=hello();
  for(int n=0;n<16;n++)network_extensions_handle(1,(word*)&m,n,TRUE);
  network_extensions_handle(1,(word*)&m,17,TRUE);network_extensions_handle(1,(word*)&m,16,FALSE);
  network_extensions_handle(NONE,(word*)&m,16,TRUE);network_extensions_handle(3,(word*)&m,16,TRUE);
  m.magic++;network_extensions_handle(1,(word*)&m,16,TRUE);m=hello();m.revision++;network_extensions_handle(1,(word*)&m,16,TRUE);
  m=hello();m.features=128;network_extensions_handle(1,(word*)&m,16,TRUE);m=hello();m.header.count=2;network_extensions_handle(1,(word*)&m,16,TRUE);
  CHECK(!sends&&!network_extensions_allows(1,250),"malformed negotiation accepted");
  role=1;m=hello();network_extensions_select_host(TRUE);network_extensions_handle(NONE,(word*)&m,16,TRUE);CHECK(!network_extensions_allows(NONE,250),"unsolicited ack");
 } else CASE("reset") {
  negotiate(EXT_ALL);CHECK(network_extensions_allows(1,250),"setup");network_extensions_machine_joined(1);CHECK(!network_extensions_allows(1,250),"reused slot");
  negotiate(EXT_ALL);network_extensions_reset();CHECK(!network_extensions_allows(1,250),"round retained capabilities");
  network_extensions_machine_joined(-2);network_extensions_machine_joined(128);
 } else CASE("transport") {
  struct {struct distributed_message_header header;long payload;} m={{0},1};
  distributed_send(&m,_distributed_message_social_line,1,sizeof(m),_distributed_to_clients_reliably);CHECK(!sends,"reliable legacy broadcast");
  distributed_send(&m,_distributed_message_social_scores,1,sizeof(m),_distributed_to_clients);CHECK(!sends,"datagram legacy broadcast");
  distributed_send_to_machine_reliably(2,&m,_distributed_message_coop_death,1,sizeof(m));CHECK(!sends,"targeted legacy send");
  negotiate(EXT_ALL);sends=0;distributed_send(&m,_distributed_message_social_line,1,sizeof(m),_distributed_to_clients_reliably);CHECK(sends==1&&recipients[0]==1,"mixed reliable send");
  sends=0;distributed_send(&m,_distributed_message_social_scores,1,sizeof(m),_distributed_to_clients);CHECK(sends==1&&recipients[0]==1,"mixed datagram send");
  sends=0;distributed_send(&m,_distributed_message_unit_states,1,sizeof(m),_distributed_to_clients);CHECK(sends==2,"core datagram changed");
  sends=0;distributed_send(&m,_distributed_message_map_share_offer,1,sizeof(m),_distributed_to_clients_reliably);CHECK(sends==2,"core reliable changed");
  role=1;sends=0;distributed_send(&m,_distributed_message_suicide_request,1,sizeof(m),_distributed_to_host_reliably);CHECK(!sends,"legacy host send");
 } else return 2;return 0;
}
