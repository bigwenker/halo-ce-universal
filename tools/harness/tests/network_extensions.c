#include "harness.h"
#include <string.h>
static void network_profile_rgb_machine_joined(long machine) { (void)machine; }
static void needler_balance_reset(void) {}
static void assault_rifle_balance_reset(void) {}
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
static int include_self;
static struct player_datum {struct {short machine_index;}network_player_data;}local_player={{7}};
static long local_player_get_player_index(short local){return 0;}
static struct player_datum *player_try_and_get(long index){return &local_player;}
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
short network_distributed_server_machines(long *out,short maximum){out[0]=1;out[1]=2;if(include_self){out[2]=7;return 3;}return 2;}
static short distributed_client_machines(long *out,short maximum){return network_distributed_server_machines(out,maximum);}
static void distributed_batch_add(short peer,void const *data,word size,word entry_size){record(peer,data,size);}
static void network_aim_assist_send_policy(long machine){if(network_extensions_allows(machine,_distributed_message_aim_assist_policy))policies++;}
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
 } else CASE("model_capability") {
  role=1;network_extensions_select_host(TRUE);network_extensions_update();
  CHECK(((struct extension_message *)wire[0])->features==255,"new AR feature not advertised");
  now+=2001;network_extensions_update();now+=2001;network_extensions_update();
  CHECK(((struct extension_message *)wire[2])->features==127,"Needler fork fallback missing");
  now+=2001;network_extensions_update();now+=2001;network_extensions_update();
  CHECK(((struct extension_message *)wire[4])->features==63,"model fork fallback missing");
  now+=2001;network_extensions_update();now+=2001;network_extensions_update();
  CHECK(((struct extension_message *)wire[6])->features==31,"old fork fallback missing");
  CHECK(!network_extensions_allows(NONE,252),"model feature enabled without acknowledgement");
  role=2;negotiate(31);CHECK(!network_extensions_allows(1,252),"model sent to old fork");
  network_extensions_reset();negotiate(63);CHECK(network_extensions_allows(1,252),"model feature not enabled");
 } else CASE("needler") {
  include_self=1;
  CHECK(!network_extensions_needler_matches(),"unknown peers enabled Needler");
  negotiate(EXT_ALL);CHECK(network_extensions_allows(1,_distributed_message_needler_policy),"missing Needler capability");
  CHECK(!network_extensions_allows(2,_distributed_message_needler_policy)&&!network_extensions_needler_matches(),"upstream peer enabled Needler");
  struct extension_message m=hello();network_extensions_handle(2,(word*)&m,sizeof(m),TRUE);
  CHECK(network_extensions_needler_matches(),"matching lobby stayed stock");
  network_extensions_machine_joined(2);CHECK(!network_extensions_needler_matches(),"late/reused peer kept Needler");
  m.features=63;network_extensions_handle(2,(word*)&m,sizeof(m),TRUE);
  CHECK(!network_extensions_needler_matches(),"old fork enabled Needler");
  role=1;CHECK(!network_extensions_needler_matches(),"client decided host rules");
 } else CASE("assault") {
  include_self=1;negotiate(EXT_ALL);
  struct extension_message m=hello();m.features=127;network_extensions_handle(2,(word*)&m,sizeof(m),TRUE);
  CHECK(network_extensions_needler_matches()&&!network_extensions_balance_matches(_distributed_message_assault_rifle_policy),"old Needler peer disabled wrong balance");
  CHECK(!network_extensions_allows(2,_distributed_message_assault_rifle_policy),"AR policy sent to old build");
  sends=0;struct {struct distributed_message_header header;word enabled,reserved;} policy={{0},1,0};
  distributed_send(&policy,_distributed_message_assault_rifle_policy,1,sizeof(policy),_distributed_to_clients_reliably);
  CHECK(sends==1&&recipients[0]==1,"AR policy escaped negotiation");
  network_extensions_machine_joined(2);m.features=EXT_ALL;network_extensions_handle(2,(word*)&m,sizeof(m),TRUE);
  CHECK(network_extensions_needler_matches()&&network_extensions_balance_matches(_distributed_message_assault_rifle_policy),"new peer did not enable both balances");
  CHECK(!network_extensions_balance_matches(_distributed_message_player_prediction),"core type accepted as balance");
 } else CASE("partial") {
  negotiate(EXT_SUICIDE);CHECK(network_extensions_allows(1,_distributed_message_suicide_request),"missing selected feature");CHECK(!network_extensions_allows(1,_distributed_message_aim_assist_policy)&&!policies,"unselected feature enabled");
  CHECK(!network_extensions_allows(2,_distributed_message_suicide_request),"other machine enabled");
 } else CASE("admission") {
  struct extension_message m=hello();
  for(int n=0;n<16;n++)network_extensions_handle(1,(word*)&m,n,TRUE);
  network_extensions_handle(1,(word*)&m,17,TRUE);network_extensions_handle(1,(word*)&m,16,FALSE);
  network_extensions_handle(NONE,(word*)&m,16,TRUE);network_extensions_handle(3,(word*)&m,16,TRUE);
  m.magic++;network_extensions_handle(1,(word*)&m,16,TRUE);m=hello();m.revision++;network_extensions_handle(1,(word*)&m,16,TRUE);
  m=hello();m.features=256;network_extensions_handle(1,(word*)&m,16,TRUE);m=hello();m.header.count=2;network_extensions_handle(1,(word*)&m,16,TRUE);
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
