#include "harness.h"
#define SET_FLAG(v,b,on) ((v)=((v)&~FLAG(b))|((on)?FLAG(b):0))
#define TICKS_PER_SECOND 30
#define _real_epsilon .0001f
#define HALO_PORT_MAXIMUM_EFFECTS 8
enum {_object_type_equipment=3,_object_type_weapon=2,_item_does_not_accelerate_bit=5,_item_chain_reaction_bit=7,_effect_chain_reaction_bit=7,_damage_chain_reaction_bit=8};
struct object_fields {long parent_object_index,owner_player_index,owner_object_index;short owner_team_index,type;};
struct object_datum {long definition_index;struct object_fields object;};
struct item_datum {long definition_index;struct object_fields object;struct {unsigned long flags;short detonation_ticks;}item;};
struct item_definition {struct {unsigned long flags;struct {long index;}detonating_effect;real detonation_delay_timer_lower_bound,detonation_delay_timer_upper_bound;}item;};
struct equipment_definition {struct {short grenade_type;}equipment;};
struct damage_data {long definition_index,owner_player_index,owner_object_index;short owner_team_index;unsigned long flags;real scale;real_point3d origin,epicenter;real_vector3d direction;long location;};
struct effect_datum {long definition_index,owner_object_index;short local_player_index;struct {short identifier;unsigned short flags;}header;long location;};
static struct item_datum grenade;static struct item_definition itemdef;static struct equipment_definition equipdef;static struct object_datum instigator;
static struct effect_datum captured;static struct damage_data caused;static boolean exists=TRUE,instigator_exists=TRUE,engine;
static struct {void *data;} pool={&captured},*effect_data=&pool;
static struct item_datum *item_get(long index){return &grenade;}
static struct item_datum *item_try_and_get(long index){return exists&&index==10?&grenade:NULL;}
static struct item_definition *item_definition_get(long index){return &itemdef;}
static void *tag_get(long group,long index){return &equipdef;}
static boolean game_engine_running(void){return engine;}
static struct object_datum *object_try_and_get(long index){return instigator_exists&&index==20?&instigator:NULL;}
static real real_random_range(real a,real b){return a;}
static void item_detonate(long index);
static boolean item_is_chain_reaction_grenade(long index);
#include "helpers.inc"
static void capture(long owner_object_index){long definition_index=6;struct effect_datum *effect=&captured;
 #include "capture.inc"
}
static void effect_new_from_object(long def,long owner,long obj,int node,real a,real b,void *c,void *field){capture(owner);}
static void item_accelerate(long index,real_vector3d const *acceleration,boolean det){if(det&&grenade.object.parent_object_index==NONE&&!engine&&TEST_FLAG(itemdef.item.flags,1))item_detonate(index);}
static void damage_data_new(struct damage_data *d,long def){memset(d,0,sizeof(*d));d->definition_index=def;d->owner_player_index=d->owner_object_index=NONE;}
static void area_of_effect_cause_damage(struct damage_data *d,long exclude){caused=*d;}
#include "items.inc"
static void blast(void){struct effect_datum *effect=&captured;real scale=1;real_point3d point={0};real_vector3d forward={1,0,0};real_point3d *world_point=&point;real_vector3d *world_forward=&forward;struct {struct {long index;}reference;}part,*part_definition=&part;part.reference.index=6;
 #include "blast.inc"
}
static void setup(void){grenade.object.parent_object_index=NONE;grenade.object.type=3;itemdef.item.flags=FLAG(1);itemdef.item.detonation_delay_timer_lower_bound=1;instigator.object.owner_player_index=0x10002;instigator.object.owner_team_index=1;grenade.object.owner_player_index=NONE;}
int main(int argc,char **argv){CHECK(argc==2,"case");char const *case_name=argv[1];setup();real_vector3d acceleration={1,0,0};struct damage_data d={.owner_player_index=0x10002,.owner_object_index=20,.owner_team_index=1};
 CASE("chain"){item_accelerate_from_damage(10,&acceleration,TRUE,&d);CHECK(grenade.item.detonation_ticks==30,"not armed");CHECK(item_is_chain_reaction_grenade(10),"not chain grenade");d.owner_object_index=30;item_accelerate_from_damage(10,&acceleration,TRUE,&d);CHECK(grenade.object.owner_object_index==20,"second blast stole instigator");exists=FALSE;instigator_exists=FALSE;blast();CHECK(TEST_FLAG(caused.flags,_damage_chain_reaction_bit),"provenance lost after deletion");CHECK(caused.owner_player_index==0x10002&&caused.owner_object_index==20,"instigator lost after deletion");}
 else CASE("ordinary"){capture(20);blast();CHECK(!TEST_FLAG(caused.flags,_damage_chain_reaction_bit)&&caused.owner_object_index==20,"ordinary thrown blast changed");}
 else CASE("guards"){engine=TRUE;item_accelerate_from_damage(10,&acceleration,TRUE,&d);CHECK(!grenade.item.detonation_ticks&&grenade.object.owner_player_index==NONE,"competitive changed");engine=FALSE;grenade.object.parent_object_index=20;item_accelerate_from_damage(10,&acceleration,TRUE,&d);CHECK(!grenade.item.detonation_ticks,"inventory detonated");grenade.object.parent_object_index=NONE;equipdef.equipment.grenade_type=NONE;item_accelerate_from_damage(10,&acceleration,TRUE,&d);CHECK(!item_is_chain_reaction_grenade(10)&&grenade.object.owner_player_index==NONE,"non-grenade classified");}
 else CASE("reuse"){item_accelerate_from_damage(10,&acceleration,TRUE,&d);capture(20);blast();CHECK(!TEST_FLAG(caused.flags,_damage_chain_reaction_bit),"ordinary reused effect inherited chain owner");}
 else CASE("restore"){item_accelerate_from_damage(10,&acceleration,TRUE,&d);effects_chain_reaction_reset();instigator_exists=FALSE;blast();CHECK(caused.owner_player_index==NONE&&caused.owner_object_index==NONE,"saved-state restore inherited future attribution");}
 else return 2;return 0;}
