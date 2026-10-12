#include "harness.h"
#include "config.inc"
struct game_variant_options { short friendly_fire; };
struct network_game { struct game_variant_options variant_options; };
struct player_datum { long unit_index; };
struct unit_datum { struct { long player_index; } unit; };
static struct game_variant_options options;
static struct network_game network_game;
static struct player_datum attacker = { 10 };
static struct unit_datum victim = {{ 2 }};
static struct { struct { boolean teams; } universal_variant; } global_variant;
static boolean coop, valid_attacker=TRUE, valid_unit=TRUE, has_game=TRUE;
static void *game_engine=(void *)1, *player_data;
enum { _object_mask_unit=1 };
static struct game_variant_options *game_variant_options_get(void) { return &options; }
static boolean network_coop_active(void) { return coop; }
static struct network_game *network_game_get_game(void) { return has_game ? &network_game : NULL; }
static void *object_try_and_get_and_verify_type(long index, long mask) { return valid_unit ? &victim : NULL; }
static void *datum_try_and_get(void *data, long index) { return valid_attacker ? &attacker : NULL; }
#include "policy.inc"

#define SET_FLAG(v,b,on) ((v) = ((v) & ~(1u << (b))) | ((on) ? 1u << (b) : 0))
enum { _object_dead_bit, _object_being_damaged_body_depleted_bit,
 _object_being_damaged_killed_instantly_bit, _damage_bypasses_shields_bit,
 _damage_skips_shields_bit, _damage_resistance_takes_shield_damage_for_children_bit,
 _damage_resistance_takes_body_damage_for_children_bit, _damage_only_hurts_shields_bit,
 _damage_resistance_only_hurt_by_explosives_bit, _damage_detonates_explosives_bit, _damage_headshot_bit, _damage_material_head_bit,
 _damage_can_cause_headshots_bit, _damage_can_cause_multiplayer_headshots_bit };
static struct { struct { unsigned long damage_flags; real body_vitality, maximum_shield_vitality; } object; } obj, *current_object=&obj;
struct resistance { unsigned long flags; };
static struct { struct resistance resistance; } collision, *collision_model=&collision;
static struct definition { unsigned long flags; } definition, *damage_definition=&definition;
static struct data { unsigned long flags; } data, *damage=&data;
static struct { unsigned long flags; } material, *damage_material=&material;
static void *object_normal;
static real shield_available, dealt_shield, dealt_body;
static int depleted;
static real chain_scale=1;
static real network_social_chain_damage_scale(struct data *damage,long index){return chain_scale;}
static void object_deplete_body(long index) { depleted++; }
static void object_damage_shield(long i, struct resistance *r, void *m, struct definition *d,
 struct data *hit, unsigned long *flags, real *shield, real *total)
{ *shield = *total < shield_available ? *total : shield_available; *total -= *shield; dealt_shield += *shield; }
static void object_damage_body(long i, long region, long node, void *normal, struct resistance *r,
 void *m, struct definition *d, struct data *hit, unsigned long *flags, real *body, real *multiplier, real total, boolean allow_instant_kill)
{ *body = total; dealt_body += total; }
static real dispatch(short friendly_damage, real friendly_damage_scale, real total_damage,
 boolean force_kill, boolean distributed_damage_authorized, short damaged_object_count)
{
 real object_total_damage, shield_damage=0, body_damage=0, body_damage_multiplier=0;
 long current_object_index=20, region_index=NONE, node_index=NONE;
 unsigned long being_damaged_flags=0;
 boolean parent_takes_body_damage=TRUE;
 #include "damage.inc"
 return total_damage;
}
static void reset(void)
{
 memset(&obj,0,sizeof(obj)); memset(&collision,0,sizeof(collision));
 memset(&definition,0,sizeof(definition)); memset(&data,0,sizeof(data));
 obj.object.maximum_shield_vitality=1; obj.object.body_vitality=1;
 dealt_body=dealt_shield=0; depleted=0; shield_available=20;
}
static int policy(void)
{
 real scale;
 CHECK(_friendly_fire_on==0 && _friendly_fire_off==1 && _friendly_fire_shields_only==2 && _friendly_fire_explosives_only==3, "friendly fire regression at line %d", __LINE__);
 for (int c=0;c<2;c++) for (int mode=0;mode<NUMBER_OF_FRIENDLY_FIRE_MODES;mode++) for (int explosive=0;explosive<2;explosive++)
 {
  coop=c; global_variant.universal_variant.teams=TRUE;
  options.friendly_fire=network_game.variant_options.friendly_fire=mode;
  short result=game_engine_friendly_damage(1,20,explosive,&scale);
  CHECK(result==(mode==_friendly_fire_off ? _friendly_damage_none : mode==_friendly_fire_shields_only ? _friendly_damage_shields : mode==_friendly_fire_explosives_only && !explosive ? _friendly_damage_none : _friendly_damage_all), "friendly fire regression at line %d", __LINE__);
  CHECK(scale==(mode==_friendly_fire_half_damage ? .5f : mode==_friendly_fire_quarter_damage ? .25f : 1.f), "friendly fire regression at line %d", __LINE__);
  CHECK(game_engine_friendly_damage(1,10,explosive,&scale)==_friendly_damage_all && scale==1, "friendly fire regression at line %d", __LINE__);
  CHECK(game_engine_friendly_damage(NONE,20,explosive,&scale)==_friendly_damage_all && scale==1, "friendly fire regression at line %d", __LINE__);
  valid_attacker=FALSE; CHECK(game_engine_friendly_damage(1,20,explosive,&scale)==_friendly_damage_all && scale==1, "friendly fire regression at line %d", __LINE__); valid_attacker=TRUE;
 }
 victim.unit.player_index=NONE;
 CHECK(game_engine_friendly_damage(1,20,FALSE,&scale)==_friendly_damage_all && scale==1, "friendly fire regression at line %d", __LINE__);
 victim.unit.player_index=2; valid_unit=FALSE;
 CHECK(game_engine_friendly_damage(1,20,FALSE,&scale)==_friendly_damage_all && scale==1, "friendly fire regression at line %d", __LINE__);
 valid_unit=TRUE; has_game=FALSE;
 CHECK(game_engine_friendly_damage(1,20,FALSE,&scale)==_friendly_damage_all && scale==1, "friendly fire regression at line %d", __LINE__);
 has_game=TRUE; coop=FALSE; global_variant.universal_variant.teams=FALSE;
 CHECK(game_engine_friendly_damage(1,20,FALSE,&scale)==_friendly_damage_all && scale==1, "friendly fire regression at line %d", __LINE__);
 return 0;
}
static int combined(void)
{
 for(int f=1;f<=4;f*=2)for(int c=1;c<=4;c++){reset();chain_scale=c*.25f;dispatch(_friendly_damage_all,1.f/f,100,FALSE,FALSE,0);CHECK(dealt_body+dealt_shield==100.f/f*chain_scale,"combined shield/body scale");}
 return 0;
}
int main(int argc,char **argv)
{
 CHECK(argc==2, "friendly fire regression at line %d", __LINE__);
 if (!strcmp(argv[1],"combined")) return combined();
 if (!strcmp(argv[1],"policy")) return policy();
 if (!strcmp(argv[1],"damage")) {
  for(int i=0;i<3;i++) { real scale=i==0?1.f:i==1?.5f:.25f; reset();
   dispatch(_friendly_damage_all,scale,100,FALSE,FALSE,0);
   CHECK(dealt_shield==20 && dealt_body==100*scale-20, "friendly fire regression at line %d", __LINE__);
   reset(); data.flags=1u<<_damage_bypasses_shields_bit;
   dispatch(_friendly_damage_all,scale,100,FALSE,FALSE,0); CHECK(dealt_shield==0 && dealt_body==100*scale, "friendly fire regression at line %d", __LINE__);
  }
  reset();dispatch(_friendly_damage_none,1,100,FALSE,FALSE,0);CHECK(dealt_shield==0 && dealt_body==0, "friendly fire regression at line %d", __LINE__);
  reset();dispatch(_friendly_damage_shields,1,100,FALSE,FALSE,0);CHECK(dealt_shield==20 && dealt_body==0, "friendly fire regression at line %d", __LINE__);
  return 0;
 }
 if (!strcmp(argv[1],"parent")) { reset(); collision.resistance.flags=1u<<_damage_resistance_takes_shield_damage_for_children_bit;
  CHECK(dispatch(_friendly_damage_all,.5f,100,FALSE,FALSE,1)==60, "friendly fire regression at line %d", __LINE__); CHECK(dealt_shield==20 && dealt_body==0, "friendly fire regression at line %d", __LINE__);
  shield_available=0; dispatch(_friendly_damage_all,.5f,60,FALSE,FALSE,0); CHECK(dealt_body==30, "friendly fire regression at line %d", __LINE__); return 0;
 }
 if (!strcmp(argv[1],"instant")) { reset(); dispatch(_friendly_damage_all,.5f,100,TRUE,FALSE,0);CHECK(depleted==0 && obj.object.body_vitality==1, "friendly fire regression at line %d", __LINE__);
  reset(); dispatch(_friendly_damage_all,1,100,TRUE,FALSE,0);CHECK(depleted==1, "friendly fire regression at line %d", __LINE__);
  reset(); dispatch(_friendly_damage_all,.25f,100,TRUE,TRUE,0);CHECK(depleted==1, "friendly fire regression at line %d", __LINE__);return 0;
 }
 return 2;
}
