#include "harness.h"
#define HALO_CUSTOM_MODELS 64
enum {_object_type_biped=0};
struct object_datum {struct {int type;long owner_player_index;} object;} unit={{0,3}};
struct player_datum {long unit_index;} player={17};
static long stock_hands=5;
static char const *selection="model";
static struct {int ready;char id[32];long tag;} arms_assets[64];
static struct object_datum *object_try_and_get(long index) {return index==17 ? &unit:NULL;}
static struct player_datum *player_try_and_get(long index) {return index==3 ? &player:NULL;}
static char const *custom_models_player_id(long index) {(void)index;return selection;}
#include "selection.inc"
int main(int argc,char **argv)
{
    char const *case_name=argc>1 ? argv[1]:"";arms_assets[1].ready=1;strcpy(arms_assets[1].id,"model");arms_assets[1].tag=9;
    CASE("selected"){CHECK(custom_models_first_person_model(17,5)==9,"custom FP not selected");}
    else CASE("missing"){arms_assets[1].ready=0;CHECK(custom_models_first_person_model(17,5)==5,"missing FP changed arms");}
    else CASE("wrong_model"){CHECK(custom_models_first_person_model(17,6)==6,"weapon/unrelated model changed");}
    else CASE("npc"){unit.object.owner_player_index=NONE;CHECK(custom_models_first_person_model(17,5)==5,"NPC selected custom arms");}
    else CASE("stale"){player.unit_index=18;CHECK(custom_models_first_person_model(17,5)==5,"stale unit inherited profile");}
    else CASE("chief"){selection="master_chief";CHECK(custom_models_first_person_model(17,5)==5,"Chief changed");}
    else CASE("other_profile"){selection="other";CHECK(custom_models_first_person_model(17,5)==5,"another profile inherited FP");}
    else return 2;return 0;
}
