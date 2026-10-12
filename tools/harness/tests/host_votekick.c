/* The public request, server kick validation and reliable red notice in a fake network. */
#include "harness.h"
#include <stdarg.h>
typedef unsigned short word;
enum { _game_connection_local, _game_connection_network_client, _game_connection_network_server,
    _distributed_message_votekick, _distributed_message_notice,
    _distributed_to_host_reliably, _distributed_to_clients_reliably,
    _kick_rejoinable, MAXIMUM_NETWORK_MACHINE_COUNT = 4,
    MAXIMUM_TRACKED_PLAYERS = 16, MAXIMUM_NOTICE_LENGTH = 160,
    REQUEST_INTERVAL_MILLISECONDS = 1000, VOTE_GAP_SECONDS = 30, _error_log };
#define VALID_INDEX(i,n) ((i)>=0 && (i)<(n))
#define csmemset memset
#define csmemcpy memcpy
#define csstrlen strlen
struct distributed_message_header { byte bytes[8]; };
#include "config.inc"
struct player_datum { boolean quit_out_of_game; };
struct network_game_server { int client_machines[MAXIMUM_NETWORK_MACHINE_COUNT]; };
static struct network_game_server server;
static struct player_datum player;
static struct { boolean active; long target_machine; } votekick;
static boolean votekick_gap, votekick_requested[4];
static unsigned long votekick_next_allowed, votekick_request_times[4];
static long network_game_server_kick_pending[4];
static int connection = _game_connection_network_server, machine = 1;
static boolean valid_player = TRUE, local_player, joined = TRUE, local_machine, server_present = TRUE;
static int warning_count, notice_count, status_count, vote_count, ban_count, packet_type, packet_direction;
static char warning[160], sent_text[160];
static byte packet_player;
static int game_connection(void) { return connection; }
static struct player_datum *distributed_player(short i) { return i == 2 && valid_player ? &player : NULL; }
static long distributed_player_machine(short i) { (void)i; return machine; }
static boolean distributed_player_is_local(short i) { (void)i; return local_player; }
static byte distributed_player_to_byte(short i) { return (byte)i; }
static unsigned long system_milliseconds(void) { return 100000; }
static void votekick_send_status(void) { status_count++; }
static void console_warning(char const *format, ...)
{
    va_list args; va_start(args,format); vsnprintf(warning,sizeof(warning),format,args); va_end(args); warning_count++;
}
static void error(int level, char const *format, ...) { (void)level; (void)format; }
static void distributed_send(void const *p, int type, int count, word size, int direction)
{
    char const *payload = (char const *)p + sizeof(struct distributed_message_header);
    (void)count; (void)size; packet_type=type; packet_direction=direction;
    if (type == _distributed_message_notice) { notice_count++; snprintf(sent_text,sizeof(sent_text),"%s",payload); }
    else packet_player=(byte)*payload;
}
static boolean distributed_game_in_progress(void) { return TRUE; }
static void distributed_printable(char *out, int n, char const *in) { snprintf(out,n,"%s",in); }
static struct network_game_server *global_network_game_server_get(void) { return server_present ? &server : NULL; }
static boolean network_game_server_client_machine_is_joined_to_game(struct network_game_server *s, int *m)
{ (void)s; (void)m; return joined; }
static boolean network_game_server_client_machine_is_local(struct network_game_server *s, int *m)
{ (void)s; (void)m; return local_machine; }
static void network_game_server_machine_names(struct network_game_server *s, long m, char *names, long size)
{ (void)s; (void)m; snprintf(names,size,"Guest"); }
static void network_game_server_ban_machine(long m, char const *names) { (void)m; (void)names; ban_count++; }
static boolean votekick_machine_valid(long m) { return VALID_INDEX(m,4); }
static boolean votekick_time_reached(unsigned long now, unsigned long at) { return (long)(now-at)>=0; }
/* A remote request enters the existing vote rules, never the direct kick. */
static void votekick_host_request(long m, short p) { (void)m; (void)p; vote_count++; }
#include "under_test.inc"

int main(int argc, char **argv)
{
    char const *case_name=argc>1 ? argv[1] : "";
    for (int i=0;i<4;i++) network_game_server_kick_pending[i]=NONE;
    CASE("host")
    {
        CHECK(network_votekick_request(2),"host request failed");
        CHECK(network_game_server_kick_pending[1]==_kick_rejoinable,"not an immediate, rejoinable kick");
        CHECK(vote_count==0 && ban_count==0 && status_count==0,"entered vote/ban pipeline");
        CHECK(notice_count==1 && warning_count==1,"duplicate or missing announcement");
        return 0;
    }
    CASE("active-target")
    {
        votekick.active=TRUE; votekick.target_machine=1;
        CHECK(network_votekick_request(2),"kick failed");
        CHECK(!votekick.active && status_count==1 && votekick_gap,"vote not ended: departure would pass/ban");
        CHECK(votekick_next_allowed==system_milliseconds()+VOTE_GAP_SECONDS*1000,"vote gap deadline lost");
        CHECK(ban_count==0 && vote_count==0 && notice_count==1,"extra vote/ban/notice");
        return 0;
    }
    CASE("other-vote")
    {
        votekick.active=TRUE; votekick.target_machine=3;
        CHECK(network_votekick_request(2),"kick failed");
        CHECK(votekick.active && status_count==0,"cancelled another player's vote"); return 0;
    }
    CASE("invalid")
    {
        CHECK(!network_votekick_request(-1),"invalid index accepted");
        valid_player=FALSE; CHECK(!network_votekick_request(2),"missing player accepted"); valid_player=TRUE;
        player.quit_out_of_game=TRUE; CHECK(!network_votekick_request(2),"quit player accepted"); player.quit_out_of_game=FALSE;
        votekick.active=TRUE; votekick.target_machine=1;
        joined=FALSE; CHECK(!network_votekick_request(2),"unjoined machine accepted"); joined=TRUE;
        machine=4; CHECK(!network_votekick_request(2),"invalid machine accepted"); machine=1;
        server_present=FALSE; CHECK(!network_votekick_request(2),"no server accepted");
        CHECK(notice_count==0 && votekick.active && status_count==0,"failed kick announced/cancelled vote");
        CHECK(network_game_server_kick_pending[1]==NONE,"failed kick dropped player"); return 0;
    }
    CASE("own-machine")
    {
        local_player=TRUE; CHECK(!network_votekick_request(2),"own player kicked"); local_player=FALSE;
        machine=NONE; CHECK(!network_votekick_request(2),"host machine kicked"); machine=1;
        local_machine=TRUE; CHECK(!network_votekick_request(2),"local server machine kicked");
        CHECK(notice_count==0 && network_game_server_kick_pending[1]==NONE,"self kick announced/queued"); return 0;
    }
    CASE("client")
    {
        connection=_game_connection_network_client;
        CHECK(network_votekick_request(2),"client request rejected");
        CHECK(packet_type==_distributed_message_votekick && packet_direction==_distributed_to_host_reliably && packet_player==2,"wrong vote packet");
        CHECK(!network_votekick_host_kick(2,FALSE),"client gained host authority");
        CHECK(notice_count==0 && network_game_server_kick_pending[1]==NONE,"client kicked directly");
        connection=_game_connection_local; CHECK(!network_votekick_request(2),"offline request accepted"); return 0;
    }
    CASE("remote")
    {
        struct distributed_votekick_request r={2,{0,0,0}};
        network_votekick_handle_request(NONE,&r,TRUE);
        network_votekick_handle_request(0,&r,FALSE);
        CHECK(vote_count==0,"forged host/UDP request accepted");
        network_votekick_handle_request(0,&r,TRUE);
        CHECK(vote_count==1 && notice_count==0 && network_game_server_kick_pending[1]==NONE,"remote request bypassed voting");
        network_votekick_handle_request(0,&r,TRUE); CHECK(vote_count==1,"rate limit lost");
        connection=_game_connection_network_client; network_votekick_handle_request(1,&r,TRUE);
        CHECK(vote_count==1,"client handled host vote pipeline"); return 0;
    }
    CASE("notice")
    {
        CHECK(network_votekick_request(2),"kick failed");
        CHECK(!strcmp(warning,"Guest was kicked by host") && !strcmp(sent_text,warning),"wrong local/client notice: %s",warning);
        CHECK(packet_type==_distributed_message_notice && packet_direction==_distributed_to_clients_reliably && warning_count==1,"red warning/reliable broadcast missing"); return 0;
    }
    CHECK(FALSE,"unknown case: %s",case_name);
}
