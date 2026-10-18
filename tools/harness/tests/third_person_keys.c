#include "harness.h"
typedef int BOOL;
typedef unsigned long Uint64;
#define MAXIMUM_BINDINGS 2
#include "keys.inc"
struct platform_input_state {int keys[8];int mouse_released;float mouse_dx,mouse_dy,mouse_wheel;};
static int bindings[NUMBER_OF_HALO_KEYBOARD_ACTIONS][MAXIMUM_BINDINGS];
static int mouse_lock,third_person_active,third_person_distance_pressed;
static float mouse_pending_x,mouse_pending_y,mouse_wheel_accumulated;
static unsigned long mouse_polls_unconsumed;
static Uint64 mouse_aimed_ms,wheel_moved_ms;
static BOOL chat;
static void pthread_mutex_lock(int *p){}
static void pthread_mutex_unlock(int *p){}
static void bindings_read(void){}
static BOOL input_held(struct platform_input_state const *p,int code){return code&&p->keys[code];}
static Uint64 SDL_GetTicks(void){return 100;}
static int network_chat_typing(void){return chat;}
#include "under_test.inc"
static void sample(int key,int near,int far,int playing){struct platform_input_state s={0};s.keys[1]=key;s.keys[2]=near;s.keys[3]=far;third_person_keys_update(&s,playing);}
int main(int argc,char **argv){char const *case_name=argc>1?argv[1]:"";
 bindings[HALO_KEYBOARD_THIRD_PERSON][0]=1;
 bindings[HALO_KEYBOARD_CAMERA_ZOOM_IN][0]=2;
 bindings[HALO_KEYBOARD_CAMERA_ZOOM_OUT][0]=3;
 CASE("hold") {int n;sample(1,0,0,TRUE);CHECK(platform_third_person_key()==1,"initial press lost");for(n=0;n<240;n++)sample(1,0,0,TRUE);CHECK(!platform_third_person_key(),"held key repeated");sample(0,0,0,TRUE);sample(1,0,0,TRUE);CHECK(platform_third_person_key(),"next press lost");}
 else CASE("context") {sample(1,0,0,TRUE);sample(1,0,0,FALSE);CHECK(!platform_third_person_key(),"pending input survived menu/chat/console");sample(1,0,0,TRUE);CHECK(!platform_third_person_key(),"held key toggled after text");sample(0,0,0,TRUE);sample(1,0,0,TRUE);CHECK(platform_third_person_key(),"new gameplay press lost");}
 else CASE("distance") {int n;sample(0,1,0,TRUE);CHECK(!platform_third_person_distance(),"first-person distance queued");sample(0,0,0,TRUE);platform_third_person_active(TRUE);sample(0,1,0,TRUE);CHECK(platform_third_person_distance()==-1,"near press");for(n=0;n<240;n++)sample(0,1,0,TRUE);CHECK(!platform_third_person_distance(),"held distance repeated");sample(0,0,1,TRUE);CHECK(platform_third_person_distance()==1,"far press");sample(0,0,0,TRUE);sample(0,1,1,TRUE);CHECK(!platform_third_person_distance(),"opposite keys did not cancel");}
 else CASE("deactivate") {platform_third_person_active(TRUE);sample(0,1,0,TRUE);platform_third_person_active(FALSE);CHECK(!platform_third_person_distance(),"camera exit kept distance");platform_third_person_active(TRUE);sample(0,0,1,TRUE);sample(0,0,0,FALSE);CHECK(!platform_third_person_distance(),"menu kept distance");}
 else CASE("wheel") {struct platform_input_state s={0};s.mouse_wheel=1;platform_third_person_active(TRUE);mouse_poll(&s);CHECK(mouse_wheel_accumulated==1&&wheel_moved_ms==100,"third person swallowed weapon wheel");chat=TRUE;mouse_poll(&s);CHECK(mouse_wheel_accumulated==0,"wheel escaped chat ownership");}
 else return 2;return 0;
}
