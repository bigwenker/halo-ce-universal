#include "harness.h"
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 128
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define PIN(x,a,b) MIN(MAX(x,a),b)
#define csmemset memset
#define _game_connection_network_server 2
#define _game_team_player 0
struct network_game {int players[128];};
struct encounter_datum {short team_index;};
static struct network_game game;
static struct encounter_datum encounter={1};
static struct {long actual_count;} actors,*actor_data=&actors;
static struct {short mode,percent,multiplier;} coop_enemies;
static int connection=2,coop=1,percent=50,multiplier=2;
static char const *mode="per_player";
static int game_connection(void){return connection;}
static int network_coop_active(void){return coop;}
static struct network_game *network_game_get_game(void){return &game;}
static int network_player_is_valid(int const *p){return *p;}
static struct encounter_datum *encounter_get(long i){(void)i;return &encounter;}
static int game_team_is_enemy(short a,short b){return a!=b;}
static char const *config_string(char const *name){(void)name;return mode;}
static long config_integer(char const *name){return !strcmp(name,"network.coop_enemies_multiplier")?multiplier:percent;}
#include "under_test.inc"
int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";
 coop_enemies_new_game();
 CASE("counts") {
  for(int pct=25;pct<=200;pct+=25){
   percent=pct;coop_enemies_new_game();memset(&game,0,sizeof(game));
   for(int p=1;p<=128;p++){
    game.players[p-1]=1;
    for(int n=1;n<=100;n++){
     long expected=MIN(((long)n*pct*(p-1)+50)/100,7L*n);
     CHECK(coop_enemies_extra_count(0,n)==expected,"p%d pct%d n%d",p,pct,n);
    }
   }
  }
 } else CASE("guards") {
  memset(&game,1,sizeof(game));percent=200;coop_enemies_new_game();
  actors.actual_count=MAXIMUM_ACTORS-COOP_ENEMIES_LEVEL_ACTORS-3;
  CHECK(coop_enemies_extra_count(0,10)==3,"actor reserve consumed");actors.actual_count=MAXIMUM_ACTORS;
  CHECK(coop_enemies_extra_count(0,10)==0,"full pool");actors.actual_count=0;
  encounter.team_index=0;CHECK(coop_enemies_extra_count(0,10)==0,"allies grew");encounter.team_index=1;
  connection=1;CHECK(coop_enemies_extra_count(0,10)==0,"client grew enemies");connection=2;
  coop=0;CHECK(coop_enemies_extra_count(0,10)==0,"not co-op");coop=1;
  mode="none";coop_enemies_new_game();CHECK(coop_enemies_extra_count(0,10)==0,"disabled");
  mode="multiplier";multiplier=32;
  coop_enemies_new_game();CHECK(coop_enemies_extra_count(0,10)==310,"static multiplier changed");
 } else CASE("config") {
  percent=50;coop_enemies_new_game();memset(&game,0,sizeof(game));game.players[0]=1;
  CHECK(coop_enemies_extra_count(0,10)==0,"one player");
  for(int p=1;p<12;p++)game.players[p]=1;
  CHECK(coop_enemies_extra_count(0,10)==55,"default twelve players");
 } else return 2;
 return 0;
}
