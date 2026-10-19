#include "harness.h"
#include <math.h>
#include <wchar.h>
#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
static boolean passenger_camera_enabled(short n){return FALSE;}
#define PIN(n,a,b) ((n)<(a)?(a):(n)>(b)?(b):(n))
#define csmemset memset
#define ABS(v) ((v)<0?-(v):(v))
#define REAL_MAX 3.402823466e38f
#define _pi 3.14159265358979323846f
#define match_assert_valid_real_vector3d_axes2(...) ((void)0)
typedef struct {real i,j;} real_vector2d;
typedef struct {real yaw,pitch;} real_euler_angles2d;
struct unit_control_data {real_vector3d looking_vector,facing_vector,aiming_vector;};
struct observer_command {long flags;real_vector3d focus_position,focus_offset;real focus_distance,field_of_view;real_vector3d forward,up,focus_velocity;};
struct player_datum {long unit_index;short local_player_index;};
struct unit_datum {struct {long parent_object_index,damage_flags;}object;struct {short current_zoom_level;}unit;};
enum {_object_dead_bit};
#include "observer_flags.inc"
static struct player_datum player={1,0};static struct unit_datum unit={{NONE,0},{NONE}};
static boolean has_player=TRUE,has_unit=TRUE,multiplayer,cinematic,enabled;
static int key;static int distance;static real_vector3d direction={1,0,0};
static int notices;static wchar_t notice[64];
static void hud_print_message(short n,wchar_t const *text){CHECK(n==0,"notice sent to another local player");notices++;wcscpy(notice,text);}
static long local_player_get_player_index(short n){return has_player?0:NONE;}
static struct player_datum *player_try_and_get(long n){return n==0&&has_player?&player:NULL;}
static struct unit_datum *unit_try_and_get(long n){return n==1&&has_unit?&unit:NULL;}
static int platform_third_person_key(void){int k=key;key=0;return k;}
static void platform_third_person_active(int a){enabled=a;}
static int platform_third_person_distance(void){int r=distance;distance=0;return r;}
static boolean game_engine_running(void){return multiplayer;}
static boolean cinematic_in_progress(void){return cinematic;}
static void player_control_get_facing_direction(short n,real_vector3d *v){*v=direction;}
static real_vector3d head_direction={1,0,0};
static void unit_get_looking_vector(long n,real_vector3d *v){*v=head_direction;}
static real normalize3d(real_vector3d *v){real n=sqrtf(v->i*v->i+v->j*v->j+v->k*v->k);if(n>0){v->i/=n;v->j/=n;v->k/=n;}return n;}
static void normalize2d(real_vector2d *v){real n=sqrtf(v->i*v->i+v->j*v->j);if(n>0){v->i/=n;v->j/=n;}}
static void cross_product3d(real_vector3d const *a,real_vector3d const *b,real_vector3d *c){*c=(real_vector3d){a->j*b->k-a->k*b->j,a->k*b->i-a->i*b->k,a->i*b->j-a->j*b->i};}
static void scale_vector3d(real_vector3d const *v,real n,real_vector3d *o){*o=(real_vector3d){v->i*n,v->j*n,v->k*n};}
static void unit_get_camera_position(long n,real_vector3d *v){*v=(real_vector3d){10,20,30};}
static real player_control_get_field_of_view(short n){return 1.2f;}

static void object_get_velocities(long n,real_vector3d *v,void *angular){*v=(real_vector3d){1,2,3};}
static void player_aiming_vector_from_facing(long n,real_vector3d *v,real_euler_angles2d const *f){*v=(real_vector3d){cosf(f->yaw),sinf(f->yaw),f->pitch};}
typedef short director_perspective;
typedef void (*director_camera_update_proc)(void*,void*,void*);
struct director {director_camera_update_proc camera_proc;short seat_state;byte camera_data[128];real debug_input_scale,camera_change_pause;boolean debug_controls;};
struct first_person_camera {int unused;};struct following_camera {int unused;};
static void first_person_camera_update(void*a,void*b,void*c){}
static void following_camera_update(void*a,void*b,void*c){}
static void first_person_camera_new(struct first_person_camera *p){}
static void following_camera_new(struct following_camera *p){}
static struct director director={.camera_proc=first_person_camera_update};static short following;
static struct director *director_get(short n){return &director;}
static long player_control_get_unit_index(short n){return player.unit_index;}
static short director_desired_perspective(long n,director_perspective *p){*p=following;return following;}
static void scripted_camera_update(void*a,void*b,void*c){}
struct location {short cluster_index;};
static void scenario_location_from_point(struct location *l,real_point3d const *p){l->cluster_index=0;}
static boolean scenario_location_underwater(struct location const *l,real_point3d const *p,void *v){return FALSE;}
static real wall_x=1000;
static boolean observer_collision_test_with_t(real_point3d const *a,real_point3d const *b,real *t,boolean media){
 real hit=(wall_x-a->x)/(b->x-a->x);if(hit>=0&&hit<=1){*t=hit;return TRUE;}return FALSE;
}
struct observer {real_point3d focus_position;real_vector3d focus_offset;real focus_distance,field_of_view;real_vector3d forward,up;struct observer_command last_command;struct {real_point3d position;}result;};
static struct observer observation;
static struct observer *observer_get(short n){return &observation;}
#include "observer_test.inc"
#include "under_test.inc"
static real_point3d finish(struct observer_command const *c){
 observation=(struct observer){.focus_position={c->focus_position.i,c->focus_position.j,c->focus_position.k},.focus_offset=c->focus_offset,.focus_distance=c->focus_distance,.field_of_view=c->field_of_view,.forward=c->forward,.up=c->up,.last_command=*c};
 observer_postcheck(0);return observation.result.position;
}
static void on(void){key=1;CHECK(third_person_camera_wanted(0),"not enabled");}
static void free_on(void){on();key=1;third_person_camera_wanted(0);key=1;third_person_camera_wanted(0);}
int main(int argc,char **argv){char const *case_name=argc>1?argv[1]:"";
 CASE("toggle"){
  CHECK(!third_person_camera_wanted(0)&&notices==0,"default notice");on();
  CHECK(notices==1&&!wcscmp(notice,L"Entered third person"),"rear notice");
  CHECK(third_person_camera_wanted(0)&&notices==1,"repeated notice");
  key=1;CHECK(third_person_camera_wanted(0)&&third_person_mode==_third_person_front&&!third_person_free_look,"front view missing");
  CHECK(notices==2&&!wcscmp(notice,L"Entered front-facing third person"),"front notice");
  key=1;CHECK(third_person_camera_wanted(0)&&third_person_free_look,"free look missing");
  CHECK(notices==3&&!wcscmp(notice,L"Entered free look"),"free notice");
  key=1;CHECK(!third_person_camera_wanted(0)&&!third_person_free_look,"toggle off");
  CHECK(notices==4&&!wcscmp(notice,L"Returned to first person"),"first notice");
  unit.object.parent_object_index=2;key=1;third_person_camera_wanted(0);CHECK(notices==4,"vehicle notice");
  unit.object.parent_object_index=NONE;unit.unit.current_zoom_level=0;key=1;third_person_camera_wanted(0);CHECK(notices==4,"scoped notice");
  unit.unit.current_zoom_level=NONE;cinematic=TRUE;key=1;third_person_camera_wanted(0);CHECK(notices==4,"cinematic notice");
  cinematic=FALSE;multiplayer=TRUE;key=1;third_person_camera_wanted(0);CHECK(notices==4,"competitive notice");
 }
 else CASE("eligibility"){on();CHECK(!third_person_camera_wanted(1),"second local player");multiplayer=TRUE;CHECK(!third_person_camera_wanted(0)&&!enabled,"competitive allowed");multiplayer=FALSE;cinematic=TRUE;CHECK(!third_person_camera_wanted(0),"cutscene");cinematic=FALSE;unit.object.damage_flags=FLAG(_object_dead_bit);CHECK(!third_person_camera_wanted(0),"dead");unit.object.damage_flags=0;has_unit=FALSE;CHECK(!third_person_camera_wanted(0),"missing unit");has_unit=TRUE;has_player=FALSE;CHECK(!third_person_camera_wanted(0),"missing player");}
 else CASE("scope"){on();unit.unit.current_zoom_level=0;CHECK(!third_person_camera_wanted(0)&&!enabled,"scoped third person");unit.unit.current_zoom_level=NONE;CHECK(third_person_camera_wanted(0),"scope exit");}
 else CASE("distance"){short n=0;struct observer_command cmd;on();distance=-100;third_person_camera_update(NULL,&n,&cmd);CHECK(cmd.focus_distance==THIRD_PERSON_NEAREST,"near clamp");distance=100;third_person_camera_update(NULL,&n,&cmd);CHECK(cmd.focus_distance==THIRD_PERSON_FURTHEST,"far clamp");}
 else CASE("camera"){short n=0;struct observer_command cmd;on();third_person_camera_update(NULL,&n,&cmd);CHECK(TEST_FLAG(cmd.flags,_observer_command_valid_bit)&&cmd.focus_position.k==30&&cmd.focus_offset.j==THIRD_PERSON_RIGHT&&cmd.field_of_view==1.2f,"observer command");}
 else CASE("front-transform"){
  int y,p;short n=0;struct observer_command rear,front;
  on();
  for(y=0;y<16;y++)for(p=-7;p<=7;p++){
   real yaw=y*.4f,pitch=p*.2f;real_point3d rp,fp;
   third_person_mode=_third_person_follow;direction=(real_vector3d){cosf(yaw)*cosf(pitch),sinf(yaw)*cosf(pitch),sinf(pitch)};
   third_person_camera_update(NULL,&n,&rear);third_person_mode=_third_person_front;third_person_camera_update(NULL,&n,&front);
   CHECK(fabsf(front.forward.i+rear.forward.i)<1e-6f&&fabsf(front.forward.j+rear.forward.j)<1e-6f&&fabsf(front.forward.k+rear.forward.k)<1e-6f,"front yaw/pitch transform");
   CHECK(front.focus_distance==rear.focus_distance&&front.field_of_view==rear.field_of_view,"front changed distance or FOV");
   CHECK(fabsf(front.forward.i*front.up.i+front.forward.j*front.up.j+front.forward.k*front.up.k)<1e-6f&&front.up.k>=0,"front axes not upright and perpendicular");
   rp=finish(&rear);fp=finish(&front);
   CHECK(fabsf(fp.x+rp.x-20)<1e-5f&&fabsf(fp.y+rp.y-40)<1e-5f,"front orbit not opposite player");
   CHECK(fabsf(fp.z+rp.z-2*(30+THIRD_PERSON_UP))<1e-5f,"front pitch orbit mismatch");
   CHECK(fabsf(direction.i-rear.forward.i)<1e-6f,"front changed player's direction");
  }
 }
 else CASE("front-collision"){
  short n=0;struct observer_command c;real_point3d clear,blocked,restored;
  on();key=1;third_person_camera_wanted(0);third_person_camera_update(NULL,&n,&c);
  CHECK(!TEST_FLAG(c.flags,_observer_command_ignore_obstructions_bit),"front disabled collision");
  clear=finish(&c);wall_x=10.2f;blocked=finish(&c);
  CHECK(blocked.x>10&&blocked.x<clear.x&&blocked.x<=wall_x,"front camera did not shorten for wall");
  CHECK(third_person_distance==THIRD_PERSON_START,"wall overwrote preferred distance");
  wall_x=1000;restored=finish(&c);CHECK(fabsf(restored.x-clear.x)<1e-6f,"distance not restored after wall");
 }
 else CASE("free-look"){real_euler_angles2d f={0,0};struct unit_control_data c={0};free_on();third_person_camera_wanted(0);third_person_control(0,&f,&c);f.yaw=1;c.aiming_vector=(real_vector3d){0,1,0};direction=(real_vector3d){0,1,0};third_person_control(0,&f,&c);CHECK(c.aiming_vector.i==1&&c.looking_vector.i==1&&c.looking_vector.j==0,"free look changed aim");key=1;third_person_camera_wanted(0);CHECK(!third_person_frozen,"frozen after release");}
 else CASE("vehicle-transition"){free_on();director_choose_game_perspective(0,FALSE);CHECK(director.camera_proc==third_person_camera_update,"did not enter camera");unit.object.parent_object_index=2;following=TRUE;director_choose_game_perspective(0,FALSE);CHECK(!enabled&&!third_person_free_look,"vehicle kept wheel/free look");CHECK(director.camera_proc==following_camera_update,"vehicle camera not restored");following=FALSE;unit.object.parent_object_index=NONE;director_choose_game_perspective(0,FALSE);director_choose_game_perspective(0,FALSE);CHECK(director.camera_proc==third_person_camera_update,"on-foot return");}
 else CASE("scripted-transition"){real_euler_angles2d f={0,0};struct unit_control_data c={0};free_on();third_person_camera_wanted(0);third_person_control(0,&f,&c);CHECK(third_person_frozen,"fixture did not freeze");director_set_camera(0,scripted_camera_update,FALSE);CHECK(!enabled&&!third_person_free_look&&!third_person_frozen,"scripted camera kept third-person input/frozen aim");CHECK(third_person_mode != _third_person_first,"camera preference lost");}
 else CASE("secondary-view"){on();director_set_camera(1,scripted_camera_update,FALSE);CHECK(enabled,"second view disabled first player's camera input");}
 else return 2;return 0;
}
