#include "harness.h"
#include <string.h>
#include <math.h>
typedef unsigned short word;
#define DEGREES_TO_RADIANS(n) ((real)(n)*3.14159265358979323846f/180.f)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define GET_MESSAGE_TYPE(h) (((h)>>2)&3)
#define TICKS_PER_SECOND 30
#define TAG_BLOCK_GET_ELEMENT(b,i,t) (&((t *)(b)->address)[i])
enum {_game_connection_local,_game_connection_network_client,_game_connection_network_server};
enum {WEAPON_DEFINITION_TAG,PROJECTILE_DEFINITION_TAG,DAMAGE_EFFECT_DEFINITION_TAG};
enum {_distributed_message_needler_policy=253,_distributed_to_clients_reliably=1,_object_mask_weapon=4};
struct tag_block {int count;void *address;};
struct weapon_magazine_definition {short rounds_loaded_maximum,rounds_reloaded,rounds_total_initial,rounds_total_maximum;unsigned long flags;};
struct weapon_trigger_definition {real final_rate_of_fire,projectile_error_inner_cone_angle,projectile_error_angle_lower_bound,projectile_error_angle_upper_bound;struct {long index;}projectile;};
struct weapon_definition {struct {struct tag_block magazines,triggers;}weapon;};
struct projectile_definition {struct {real initial_velocity,final_velocity,guided_angular_velocity;unsigned long flags;struct {long index;}effect,super_detonation;}projectile;};
struct damage_effect_definition {real falloff_radius,cutoff_radius;struct {real area_of_effect_core_radius;}damage;};
struct weapon_magazine {short rounds_loaded,rounds_total,state,state_timer;};
struct weapon_datum {long definition_index;struct {struct weapon_magazine magazines[2];word control_flags;}weapon;struct {word flags;}item;};
struct object_iterator {int next;};
struct distributed_message_header {word header;byte type,count;long game_time;};
static struct weapon_magazine_definition magazine={20,20,80,80};
static struct weapon_trigger_definition trigger={10,.034906585f,.06981317f,.06981317f,{1}};
static struct weapon_definition gun={{{1,&magazine},{1,&trigger}}};
static struct projectile_definition needle={{4.f/30,4.f/30,DEGREES_TO_RADIANS(90),8,{4},{5}}};
static struct damage_effect_definition damage[2]={{.5f,1.f,{.25f}},{2.f,4.f,{1.f}}};
static struct weapon_datum weapons[2]={{0,{{{30,40},{0}}}},{99,{{{30,40},{0}}}}};
static int role,playing=1,menu,matching,allowed,sends,missing=NONE;
static int invalidations;
static void network_damage_invalidate_tag_cache(void){invalidations++;}
static unsigned long now=1000;
static int game_connection(void){return role;}
static boolean game_in_progress(void){return playing;}
static boolean main_menu_is_active(void){return menu;}
static boolean network_extensions_needler_matches(void){return matching;}
static boolean network_extensions_allows(long machine,byte type){return allowed;}
unsigned long system_milliseconds(void){return now;}
static void distributed_send(void *m,byte type,short count,word size,int direction){CHECK(allowed,"sent fork policy to upstream");sends++;}
static long tag_loaded(int group,char const *name){long n=group==WEAPON_DEFINITION_TAG?0:group==PROJECTILE_DEFINITION_TAG?1:strstr(name,"shock wave")?3:2;return n==missing?NONE:n;}
#define weapon_definition_get(i) (&gun)
#define projectile_definition_get(i) (&needle)
#define damage_effect_definition_get(i) (&damage[(i)-2])
static void object_iterator_new(struct object_iterator *i,unsigned long f,byte b){i->next=0;}
static void *object_iterator_next(struct object_iterator *i){return i->next<2?&weapons[i->next++]:NULL;}
#include "under_test.inc"
enum {_magazine_unchambered};
static struct {boolean infinite_ammo;}cheat;
#define weapon_get(i) (&weapons[i])
static struct weapon_magazine *weapon_magazine_get(struct weapon_datum *w,short m){return &w->weapon.magazines[m];}
static void weapon_magazine_start_reload(long w,short m,boolean b){}
#include "reload.inc"

/* The production attach and detonate branches, including their child walks. */
#define projectile_datum object_datum
#define SET_FLAG(f,b,v) ((f)=(v)?(f)|(1UL<<(b)):(f)&~(1UL<<(b)))
enum {_projectile_super_combining_explosion_bit=3,_projectile_already_super_exploded_bit=4,_projectile_will_super_explode_bit=5,_object_type_biped=0,_collision_result_object=1};
struct object_datum {long definition_index;struct {long parent_object_index,first_child_object_index,next_object_index;short type;real_point3d position;}object;struct {unsigned long flags;real arming_time,detonation_timer;}projectile;struct {long player_index;}unit;};
static struct object_datum children[12];
static int detonated;
#define object_get(i) (&children[i])
#define projectile_get(i) (&children[i])
#define biped_get(i) (&children[i])
static boolean game_engine_running(void){return FALSE;}
static real real_random(void){return .5f;}
static void object_get_origin(long i,real_point3d *p){memset(p,0,sizeof(*p));}
static void object_detach(long i){detonated++;}
#define object_translate(a,b,c) ((void)0)
#define object_force_inside_bsp(a,b) ((void)(b))
#define object_compute_node_matrices_recursive(a) ((void)0)
static void combine(int count,long definition_index)
{
 int i;
 memset(children,0,sizeof(children));detonated=0;
 for(i=0;i<=count;i++){
  children[i].definition_index=definition_index;children[i].object.next_object_index=i<count-1?i+1:NONE;
  children[i].projectile.arming_time=children[i].projectile.detonation_timer=1;
 }
 children[0].object.first_child_object_index=count>1?1:NONE;children[0].unit.player_index=NONE;
 struct object_datum *projectile=&children[count];
 struct projectile_definition const *definition=&needle;
 struct {short type;long object_index;} hit={_collision_result_object,0},*collision=&hit;
 short combining_limit=needler_balance_combining_limit(definition_index,6);
 long projectile_index=count,effect_definition_index=NONE;
 projectile->object.parent_object_index=0;
#include "attach.inc"
 CHECK(TEST_FLAG(projectile->projectile.flags,_projectile_will_super_explode_bit)==(count>=(needler_balance.enabled&&definition_index==1?8:7)),"attach threshold %d",count);
 projectile->object.next_object_index=count>1?1:NONE;children[0].object.first_child_object_index=count;
#include "detonate.inc"
 (void)effect_definition_index;
 CHECK(detonated==(count>=(needler_balance.enabled&&definition_index==1?8:7)),"detonate threshold %d",count);
}
static void check_enabled(void){
 CHECK(magazine.rounds_loaded_maximum==30&&magazine.rounds_reloaded==30,"magazine/reload");
 CHECK(trigger.final_rate_of_fire==7.5f&&trigger.projectile_error_inner_cone_angle==0,"rate/inner cone");
 CHECK(fabsf(trigger.projectile_error_angle_lower_bound-DEGREES_TO_RADIANS(.5f))<1e-6f&&fabsf(trigger.projectile_error_angle_upper_bound-DEGREES_TO_RADIANS(1.5f))<1e-6f,"spread");
 CHECK(fabsf(needle.projectile.initial_velocity-.2f)<1e-6f&&needle.projectile.final_velocity==needle.projectile.initial_velocity,"per-tick speed");
 CHECK(fabsf(needle.projectile.guided_angular_velocity-DEGREES_TO_RADIANS(22.5f))<1e-6f,"homing");
 CHECK(damage[0].cutoff_radius==.5f&&damage[1].cutoff_radius==2&&damage[0].damage.area_of_effect_core_radius==.125f,"radius");
 CHECK(magazine.rounds_total_initial==80&&magazine.rounds_total_maximum==80,"reserve changed");
}
int main(int argc,char **argv)
{
 CHECK(argc==2,"case");char const *case_name=argv[1];
 struct weapon_magazine_definition before_mag=magazine;struct weapon_trigger_definition before_trigger=trigger;
 struct projectile_definition before_needle=needle;struct damage_effect_definition before_damage[2];memcpy(before_damage,damage,sizeof(damage));
 if(!strcmp(case_name,"missing")){
  for(missing=0;missing<4;missing++){needler_balance_tags_loaded();needler_balance_reset();CHECK(!needler_balance.enabled,"partial tags patched");}
  missing=NONE;gun.weapon.magazines.count=0;needler_balance_tags_loaded();needler_balance_reset();CHECK(!needler_balance.enabled,"missing magazine");return 0;
 }
 needler_balance_tags_loaded();needler_balance_reset();
 if(!strcmp(case_name,"solo")){check_enabled();for(int i=0;i<100;i++)needler_balance_update();check_enabled();CHECK(invalidations==1,"rebuilt damage cache every frame");}
 else if(!strcmp(case_name,"restore")){
  role=2;matching=FALSE;allowed=1;needler_balance_update();
  CHECK(!memcmp(&magazine,&before_mag,sizeof(magazine))&&!memcmp(&trigger,&before_trigger,sizeof(trigger))&&!memcmp(&needle,&before_needle,sizeof(needle))&&!memcmp(damage,before_damage,sizeof(damage)),"stock not restored exactly");
  CHECK(weapons[0].weapon.magazines[0].rounds_loaded==20&&weapons[0].weapon.magazines[0].rounds_total==50,"ammo lost/duplicated");
  CHECK(weapons[1].weapon.magazines[0].rounds_loaded==30,"another weapon touched");
 }
 else if(!strcmp(case_name,"mixed")){
  role=2;matching=FALSE;allowed=1;needler_balance_update();CHECK(!needler_balance.enabled,"upstream peer");
  matching=TRUE;needler_balance_update();check_enabled();matching=FALSE;needler_balance_update();CHECK(!needler_balance.enabled&&magazine.rounds_loaded_maximum==20,"late upstream join");
 }
 else if(!strcmp(case_name,"upstream-host")){role=1;allowed=FALSE;needler_balance_update();CHECK(!needler_balance.enabled&&!sends,"changed upstream host rules");}
 else if(!strcmp(case_name,"policy")||!strcmp(case_name,"bad-policy")){
  role=1;needler_balance_reset();allowed=TRUE;
  struct needler_policy_message m={{8,_distributed_message_needler_policy,1,30},1,0};
  if(!strcmp(case_name,"bad-policy")){
   needler_balance_handle(1,(word*)&m,sizeof(m),TRUE);needler_balance_handle(NONE,(word*)&m,sizeof(m),FALSE);
   needler_balance_handle(NONE,(word*)&m,sizeof(m)-1,TRUE);m.reserved=1;needler_balance_handle(NONE,(word*)&m,sizeof(m),TRUE);m.reserved=0;
   m.enabled=2;needler_balance_handle(NONE,(word*)&m,sizeof(m),TRUE);m.enabled=1;allowed=FALSE;needler_balance_handle(NONE,(word*)&m,sizeof(m),TRUE);
   CHECK(!needler_balance.enabled,"malformed/unauthorized policy");
  }else{needler_balance_handle(NONE,(word*)&m,sizeof(m),TRUE);check_enabled();needler_balance_reset();CHECK(!needler_balance.enabled,"round carried policy");}
 }
 else if(!strcmp(case_name,"reload-map")){
  needler_balance_tags_unloaded();role=1;needler_balance_reset();CHECK(!needler_balance.magazine,"stale tag pointer");
  magazine=before_mag;trigger=before_trigger;needle=before_needle;memcpy(damage,before_damage,sizeof(damage));
  needler_balance_tags_loaded();role=0;needler_balance_reset();check_enabled();
 }
 else if(!strcmp(case_name,"reload")){
  weapons[0].item.flags=2;
  for(int initial=0;initial<30;initial++){
   weapons[0].weapon.magazines[0].rounds_loaded=initial;weapons[0].weapon.magazines[0].rounds_total=60;
   weapon_magazine_finish_reload(0,0);
   CHECK(weapons[0].weapon.magazines[0].rounds_loaded==30&&weapons[0].weapon.magazines[0].rounds_total==30+initial,"reload lost/duplicated ammo");
  }
  weapons[0].weapon.magazines[0].rounds_loaded=2;weapons[0].weapon.magazines[0].rounds_total=5;
  weapon_magazine_finish_reload(0,0);CHECK(weapons[0].weapon.magazines[0].rounds_loaded==7&&weapons[0].weapon.magazines[0].rounds_total==0,"scarce ammo reload");
 }
 else if(!strcmp(case_name,"combine")){
  for(int n=1;n<=10;n++){combine(n,1);combine(n,99);}
  role=1;needler_balance_reset();for(int n=1;n<=10;n++)combine(n,1);
 }
 else return 2;
 return 0;
}
