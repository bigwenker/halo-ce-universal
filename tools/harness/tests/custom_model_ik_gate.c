#include "harness.h"
enum {_unit_state_throw_grenade=1};
struct unit_datum {struct {struct {int state,action;} animation;} unit;} unit;
static long grenade_throw_ik_release_times[2048],now=100;
static int enabled=1;
static struct unit_datum *unit_get(long index) {(void)index;return &unit;}
static long game_time_get(void) {return now;}
static int unit_animation_enhanced(long index) {(void)index;return enabled;}
#include "gate.inc"
int main(int argc,char **argv)
{
    char const *case_name=argc>1 ? argv[1]:"";
    unit.unit.animation.state=_unit_state_throw_grenade;
    CASE("readonly"){CHECK(unit_grenade_throw_ik_release(0,FALSE),"throw not released");CHECK(!grenade_throw_ik_release_times[0],"render wrote simulation timer");}
    else CASE("record"){CHECK(unit_grenade_throw_ik_release(0,TRUE),"throw not released");CHECK(grenade_throw_ik_release_times[0]==101,"simulation timer not recorded");}
    else CASE("blend")
    {
        CHECK(unit_grenade_throw_ik_release(0,TRUE),"start throw");unit.unit.animation.state=0;now+=3;
        CHECK(unit_grenade_throw_ik_release(0,FALSE),"blend lost");CHECK(grenade_throw_ik_release_times[0]==101,"render refreshed timer");
        now+=GRENADE_THROW_BLEND_TICKS;CHECK(!unit_grenade_throw_ik_release(0,FALSE),"blend never ends");
    }
    else CASE("disabled"){enabled=0;CHECK(!unit_grenade_throw_ik_release(0,FALSE)&&!unit_grenade_throw_ik_release(0,TRUE),"disabled animations release IK");}
    else return 2;
    return 0;
}
