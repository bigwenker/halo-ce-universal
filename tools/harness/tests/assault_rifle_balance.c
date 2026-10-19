#include "harness.h"
#include <string.h>
#include <math.h>
typedef unsigned short word;
#define GET_MESSAGE_TYPE(h) (((h)>>2)&3)
#define TAG_BLOCK_GET_ELEMENT(b,i,t) (&((t *)(b)->address)[i])
enum {_game_connection_local,_game_connection_network_client,_game_connection_network_server};
enum {WEAPON_DEFINITION_TAG,PROJECTILE_DEFINITION_TAG,DAMAGE_EFFECT_DEFINITION_TAG};
enum {_distributed_message_assault_rifle_policy=254,_distributed_to_clients_reliably=1};
struct tag_block {int count;void *address;};
struct weapon_trigger_definition {real projectile_error_inner_cone_angle,projectile_error_angle_lower_bound,projectile_error_angle_upper_bound,projectile_distribution_angle;struct {long index;}projectile;real initial_rate_of_fire,final_rate_of_fire,initial_error,final_error;};
struct weapon_definition {struct {struct tag_block triggers;}weapon;};
struct projectile_definition {struct {struct {long index;}impact_damage;real initial_velocity,final_velocity;}projectile;};
struct damage_definition {real damage_minimum,damage_lower_bound,damage_upper_bound;unsigned long flags;real material_modifiers[3];};
struct damage_effect_definition {struct damage_definition damage;real falloff_radius,cutoff_radius;};
struct distributed_message_header {word header;byte type,count;long game_time;};
static struct weapon_trigger_definition trigger={1,2,6.5f,.5f,{1},15,15,0,1};
static struct weapon_definition gun={{{1,&trigger}}};
static struct projectile_definition bullet={{{2},10.8f,10.8f}};
static struct damage_effect_definition impact={{10,8,12,0x1234,{1,2,3}},0,0},unrelated={{40,50,60,0x1234,{1,2,3}},.5f,.5f};
static int role,playing=1,menu,matching,allowed,sends,invalidations,missing=NONE;
static unsigned long now=1000;
static int game_connection(void){return role;}
static boolean game_in_progress(void){return playing;}
static boolean main_menu_is_active(void){return menu;}
static boolean network_extensions_balance_matches(byte type){CHECK(type==254,"wrong balance capability");return matching;}
static boolean network_extensions_allows(long machine,byte type){CHECK(machine==NONE&&type==254,"wrong policy");return allowed;}
static void network_damage_invalidate_tag_cache(void){invalidations++;}
unsigned long system_milliseconds(void){return now;}
static void distributed_send(void *m,byte type,short count,word size,int direction){CHECK(type==254&&count==1,"wrong message");sends++;}
static long tag_loaded(int group,char const *name){CHECK(!strcmp(name,group==WEAPON_DEFINITION_TAG?"weapons\\assault rifle\\assault rifle":"weapons\\assault rifle\\bullet"),"wrong tag path");return group==missing?NONE:group;}
#define weapon_definition_get(i) (&gun)
#define projectile_definition_get(i) (&bullet)
#define damage_effect_definition_get(i) (&impact)
#include "under_test.inc"
static real random_fraction;
static real real_random_range(real a,real b){return a+(b-a)*random_fraction;}
#include "damage.inc"
static void check_enabled(void)
{
 CHECK(trigger.projectile_error_inner_cone_angle==.75f&&trigger.projectile_error_angle_lower_bound==1.5f&&trigger.projectile_error_angle_upper_bound==4.875f&&trigger.projectile_distribution_angle==.375f,"spread not 25 percent tighter");
 CHECK(fabsf(impact.damage.damage_minimum-12)<1e-5f&&fabsf(impact.damage.damage_lower_bound-9.6f)<1e-5f&&fabsf(impact.damage.damage_upper_bound-14.4f)<1e-5f,"damage bounds not 20 percent higher");
 CHECK(trigger.initial_rate_of_fire==15&&trigger.final_rate_of_fire==15&&trigger.initial_error==0&&trigger.final_error==1,"firing/bloom timing changed");
 CHECK(impact.damage.flags==0x1234&&impact.damage.material_modifiers[2]==3&&bullet.projectile.initial_velocity==10.8f,"flags/materials/projectile changed");
}
int main(int argc,char **argv)
{
 CHECK(argc==2,"case");char const *case_name=argv[1];
 struct weapon_trigger_definition before_trigger=trigger;
 struct damage_effect_definition before_damage=impact,before_unrelated=unrelated;
 if(!strcmp(case_name,"missing")){
  for(missing=0;missing<3;missing++){assault_rifle_balance_tags_loaded();assault_rifle_balance_reset();CHECK(!assault_rifle_balance.enabled&&!memcmp(&impact,&before_damage,sizeof(impact)),"partial tags patched");}
  missing=NONE;gun.weapon.triggers.count=0;assault_rifle_balance_tags_loaded();assault_rifle_balance_reset();CHECK(!assault_rifle_balance.trigger,"empty triggers");
  gun.weapon.triggers.count=1;trigger.projectile.index=99;assault_rifle_balance_tags_loaded();assault_rifle_balance_reset();CHECK(!assault_rifle_balance.trigger,"replaced projectile patched");
  trigger.projectile.index=1;bullet.projectile.impact_damage.index=99;assault_rifle_balance_tags_loaded();assault_rifle_balance_reset();CHECK(!assault_rifle_balance.trigger,"shared/replaced damage patched");return 0;
 }
 assault_rifle_balance_tags_loaded();assault_rifle_balance_reset();
 if(!strcmp(case_name,"solo")){
  check_enabled();for(int i=0;i<100;i++)assault_rifle_balance_update();check_enabled();CHECK(invalidations==1,"per-frame cache invalidation");
 }
 else if(!strcmp(case_name,"restore")||!strcmp(case_name,"cycles")){
  role=2;allowed=1;
  for(int i=0;i<100;i++){
   matching=FALSE;assault_rifle_balance_update();
   CHECK(!memcmp(&trigger,&before_trigger,sizeof(trigger))&&!memcmp(&impact,&before_damage,sizeof(impact)),"stock values drifted");
   matching=TRUE;assault_rifle_balance_update();check_enabled();now+=1000;
  }
 }
 else if(!strcmp(case_name,"mixed")){
  role=2;matching=FALSE;assault_rifle_balance_update();CHECK(!assault_rifle_balance.enabled,"mixed lobby buffed");
  matching=TRUE;assault_rifle_balance_update();check_enabled();matching=FALSE;assault_rifle_balance_update();CHECK(!assault_rifle_balance.enabled,"late upstream join");
 }
 else if(!strcmp(case_name,"upstream-host")){
  role=1;allowed=FALSE;assault_rifle_balance_update();CHECK(!assault_rifle_balance.enabled&&!sends,"upstream host changed");
 }
 else if(!strcmp(case_name,"policy")||!strcmp(case_name,"bad-policy")){
  role=1;assault_rifle_balance_reset();allowed=TRUE;
  struct assault_rifle_policy_message m={{8,254,1,30},1,0};
  if(!strcmp(case_name,"bad-policy")){
   assault_rifle_balance_handle(1,(word*)&m,sizeof(m),TRUE);assault_rifle_balance_handle(NONE,(word*)&m,sizeof(m),FALSE);
   assault_rifle_balance_handle(NONE,(word*)&m,sizeof(m)-1,TRUE);m.reserved=1;assault_rifle_balance_handle(NONE,(word*)&m,sizeof(m),TRUE);
   m.reserved=0;m.enabled=2;assault_rifle_balance_handle(NONE,(word*)&m,sizeof(m),TRUE);m.enabled=1;allowed=FALSE;assault_rifle_balance_handle(NONE,(word*)&m,sizeof(m),TRUE);
   CHECK(!assault_rifle_balance.enabled,"unauthorized/malformed policy");
  }else{assault_rifle_balance_handle(NONE,(word*)&m,sizeof(m),TRUE);check_enabled();assault_rifle_balance_reset();CHECK(!assault_rifle_balance.enabled,"stale round policy");}
 }
 else if(!strcmp(case_name,"reload-map")){
  assault_rifle_balance_tags_unloaded();role=1;assault_rifle_balance_reset();CHECK(!assault_rifle_balance.trigger,"stale pointers");
  trigger=before_trigger;impact=before_damage;assault_rifle_balance_tags_loaded();role=0;assault_rifle_balance_reset();check_enabled();
 }
 else if(!strcmp(case_name,"damage")){
  for(int s=0;s<=10;s++)for(int r=0;r<=10;r++)for(int m=1;m<=4;m++){
   random_fraction=r/10.f;
   real scale=s/10.f,multiplier=m*.5f;
   real original=((1-scale)*10+(8+4*random_fraction)*scale)*multiplier;
   CHECK(fabsf(actual_damage(scale,multiplier)-original*1.2f)<1e-4f,"damage pipeline not uniformly 20 percent stronger");
  }
 }
 else return 2;
 CHECK(!memcmp(&unrelated,&before_unrelated,sizeof(unrelated)),"melee or another weapon changed");return 0;
}
