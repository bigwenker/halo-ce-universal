#include "harness.h"
#define SET_FLAG(v,b,on) ((v)=((v)&~FLAG(b))|((on)?FLAG(b):0))
enum {_damage_material_head_bit,_damage_can_cause_headshots_bit,_damage_can_cause_multiplayer_headshots_bit,_object_being_damaged_killed_instantly_bit,_object_being_damaged_force_hard_ping_bit};
enum {_object_type_biped};
static boolean engine;static boolean game_engine_running(void){return engine;}
static struct {struct {long player_index;}unit;}unit;
#define unit_get(index) (&unit)
static void headshot(boolean allow_instant_kill,boolean player,boolean multiplayer,real scale,boolean instakill,boolean double_headshot,real *body,real *dealt,unsigned long *being_damaged_flags)
{
 struct {struct {int type;real body_vitality;}object;}obj,*object=&obj;
 struct {unsigned long flags;}mat,*damage_material=&mat,def,*damage_definition=&def;
 long object_index=1;real damage_amount=10*scale,actual_damage=damage_amount;
 engine=multiplayer;unit.unit.player_index=player?1:NONE;object->object.type=0;object->object.body_vitality=100;
 damage_material->flags=FLAG(_damage_material_head_bit);damage_definition->flags=(instakill?FLAG(_damage_can_cause_headshots_bit):0)|(double_headshot?FLAG(_damage_can_cause_multiplayer_headshots_bit):0);
 #include "under_test.inc"
 *body=object->object.body_vitality-actual_damage;*dealt=actual_damage;
}
int main(void){real body,dealt;unsigned long flags=0;
 for(int scale=1;scale<=4;scale*=2){headshot(scale==1,TRUE,TRUE,1.f/scale,TRUE,FALSE,&body,&dealt,&flags);CHECK(body==(scale==1?-10.f:100-10.f/scale),"friendly headshot bypassed scale%d",scale);}
 headshot(TRUE,FALSE,FALSE,1,TRUE,FALSE,&body,&dealt,&flags);CHECK(body==-10,"AI headshot changed");
 headshot(TRUE,TRUE,FALSE,1,TRUE,FALSE,&body,&dealt,&flags);CHECK(body==90,"campaign player instant headshot");
 headshot(FALSE,TRUE,TRUE,.5f,FALSE,TRUE,&body,&dealt,&flags);CHECK(body==90&&dealt==10,"numerical headshot multiplier lost");return 0;
}
