#include "harness.h"
#include <math.h>
#include <stdint.h>
#include "types.inc"
#define BOOL int
#define HALO_KEYBOARD_SPRAY 18
#define _real_epsilon 0.00001f
#define _pi 3.14159265358979323846f
#define _x 0
#define _y 1
#define _z 2
#define CLIP_BUFFER_SIZE 64
#define NUMBER_OF_VERTICES_PER_TRIANGLE 3
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define PIN(x,a,b) MIN(MAX(x,a),b)
#define DEGREES_TO_RADIANS(x) ((x)*(_pi/180.0f))
#define VALID_INDEX(x,n) ((unsigned long)(x)<(unsigned long)(n))
#define SET_FLAG(x,b,on) ((on)?((x)|=FLAG(b)):((x)&=~FLAG(b)))
#define csmemset memset
#define csmemcpy memcpy
#define _error_silent 0
static void error(int level,const char *format,...){(void)level;(void)format;}
static boolean decals_enabled=TRUE, decal_collision_edge_vertices_reported;
static real_point2d decal_points2d_temp[2][12];
static struct {float zoffset;} rasterizer_debug_options;
static struct decal_wrap_parameters const decal_wrap_parameters[4]={
 {40,110,1.5,TRUE,{0}}, {40,110,1.5,TRUE,{0}}, {40,110,1.5,TRUE,{0}}, {10,10,1.5,FALSE,{0}}};
struct tag_block {long count;void *address;};
struct collision_bsp {struct {real_plane3d plane;} bsp3d;struct tag_block surfaces,edges,vertices;};
static struct collision_bsp bsp;
static struct collision_surface surfaces[2];
static struct collision_edge edges[8];
static struct collision_vertex vertices[8];
#define TAG_BLOCK_GET_ELEMENT(b,i,t) (&((t *)(b)->address)[i])
static struct collision_bsp *global_collision_bsp_get(void){return &bsp;}
static boolean collision_bsp_valid_surface_index(struct collision_bsp const *b,long i){return VALID_INDEX(i,b->surfaces.count);}
static boolean collision_surface_edge_ring_continues(struct collision_bsp const *b,long edge,short iteration){return VALID_INDEX(edge,b->edges.count)&&iteration<8;}
#define bsp3d_get_plane_from_designator(b,d,p) (*(p)=(b)->plane,(void)(d))
struct collision_result {short type;real_point3d point;real_plane3d plane;long surface_index;};
struct render_camera {real_point3d position;real_vector3d forward;};
struct render_frustum {real_matrix4x3 world_to_view;boolean projection_valid;real projection_matrix[4][4];};
static boolean active=TRUE, menu, paused, cinematic, request, blocked, missing;
static short connection, players=1;
static long unit=4, tick;
#define TICKS_PER_SECOND 30
#define SOUND_DEFINITION_TAG 1
static int sounds, choice;
static long game_time_get(void){return tick;}
static long tag_loaded(long group,const char *name){CHECK(group==SOUND_DEFINITION_TAG && !strcmp(name,"sound\\sfx\\weapons\\plasma rifle\\overheat"),"wrong sound tag");return 7;}
static short local_random_range(short a,short b){CHECK(a==0&&b==2,"wrong random range");return choice;}
static long unspatialized_impulse_sound_new_named(long index,const char *name){CHECK(index==7 && !strcmp(name,choice?"overheat3":"overheat2"),"wrong sound variant");sounds++;return 8;}
static int loads,draws,forgotten,rays,presses,saves;
static boolean upload_failed;
static boolean source_preparing;
static int queued[SPRAY_SHARE_SLOTS];
static struct collision_result hit;
static boolean game_in_progress(void){return active;}
static boolean main_menu_is_active(void){return menu;}
static short game_connection(void){return connection;}
static short local_player_count(void){return players;}
static boolean game_time_get_paused(void){return paused;}
static boolean cinematic_in_progress(void){return cinematic;}
static long player_control_get_unit_index(short p){(void)p;return unit;}
int platform_spray_take_request(void){boolean old=request;request=FALSE;return old;}
void platform_spray_request(void){request=TRUE;presses++;}
void halo_spray_images_update(void) {}
int halo_spray_source_acquire(struct halo_spray_source *source){memset(source,0,sizeof(*source));return source_preparing?-1:!missing;}
void halo_spray_source_release(struct halo_spray_source *source){(void)source;}
static void console_printf(boolean warning,const char *format,...){(void)warning;(void)format;}
int halo_spray_image_enqueue(int slot,const void *data,size_t size,const unsigned char hash[32],const char *name,int save){
 (void)hash;(void)name;CHECK(slot>=0&&slot<SPRAY_SHARE_SLOTS&&data&&size,"invalid queued image");
 if(upload_failed)return 0;if(!queued[slot]){queued[slot]=1;loads++;saves+=save;}return 1;
}
int halo_spray_image_load(float *aspect){loads++;*aspect=1;return !missing;}
void halo_spray_image_forget(void){forgotten++;memset(queued,0,sizeof(queued));}
static short global_structure_bsp_index_get(void){return 0;}
void network_spray_reset(void){}
long network_spray_unit(int owner){(void)owner;return unit;}
int network_spray_publish(struct spray_pose pose){(void)pose;return 0;}
int network_spray_is_local(int owner){return owner==0;}
int halo_spray_image_aspect(const void *data,size_t size,float *aspect){(void)data;(void)size;*aspect=1;return !missing;}
int halo_spray_file_save(const void *data,size_t size,const char *name,char *path,size_t capacity,float *aspect){(void)data;(void)size;(void)name;(void)capacity;saves++;strcpy(path,"fake.png");*aspect=1;return 1;}
int halo_spray_image_load_bytes(int slot,const void *data,size_t size,float *aspect){CHECK(slot>=0&&slot<SPRAY_SHARE_SLOTS&&data&&size,"invalid memory image");loads++;*aspect=1;return !upload_failed;}
int halo_spray_image_load_slot(int slot,const char *path,float *aspect){(void)slot;(void)path;return halo_spray_image_load(aspect);}
static void crypto_blake2b(uint8_t *hash,size_t count,const uint8_t *data,size_t size){(void)data;(void)size;memset(hash,1,count);}
void halo_spray_draw(struct halo_spray_clip_vertex const *v,int count){CHECK(count>0&&count<=HALO_SPRAY_MAXIMUM_VERTICES,"bad draw size");CHECK(isfinite(v[0].position[0]),"invalid projection");draws++;}
void halo_spray_draw_slot(int slot,struct halo_spray_clip_vertex const *v,int count){(void)slot;halo_spray_draw(v,count);}
static boolean collision_test_vector(unsigned long flags,real_point3d const *p,real_vector3d const *v,long ignore,struct collision_result *out){
 if(ignore==NONE){CHECK(flags&FLAG(_collision_test_structure_bit),"remote projection omitted structure");CHECK(!(flags&FLAG(_collision_test_objects_bit)),"moving objects can invalidate received placement");*out=hit;return TRUE;}
 CHECK(flags&FLAG(_collision_test_objects_bit),"objects must block sprays");CHECK(ignore==unit,"self not ignored");
 CHECK(fabs(v->i)==1.5f&&v->j==0&&v->k==0,"bad reach or direction");(void)p;rays++;*out=hit;return !blocked;
}
static real_point3d *matrix4x3_transform_point(real_matrix4x3 const *m,real_point3d const *p,real_point3d *out){(void)m;*out=*p;return out;}
#include "under_test.inc"

static void wall(float half)
{
 memset(&bsp,0,sizeof(bsp));memset(edges,0,sizeof(edges));memset(vertices,0,sizeof(vertices));memset(surfaces,0,sizeof(surfaces));
 bsp.surfaces=(struct tag_block){1,surfaces};bsp.edges=(struct tag_block){4,edges};bsp.vertices=(struct tag_block){4,vertices};
 bsp.bsp3d.plane.n.i=1;surfaces[0].first_edge_index=0;
 float yz[4][2]={{-half,-half},{half,-half},{half,half},{-half,half}};
 for(int i=0;i<4;i++){vertices[i].point.y=yz[i][0];vertices[i].point.z=yz[i][1];edges[i].vertex_indices[0]=i;edges[i].vertex_indices[1]=(i+1)%4;edges[i].edge_indices[0]=(i+1)%4;edges[i].surface_indices[0]=0;edges[i].surface_indices[1]=NONE;}
 memset(&hit,0,sizeof(hit));hit.type=_collision_result_structure;hit.plane=bsp.bsp3d.plane;
}
int main(int argc,char **argv)
{
 const char *case_name=argc>1?argv[1]:"";wall(1);
 struct halo_spray_vertex output[HALO_SPRAY_MAXIMUM_VERTICES];
 struct render_camera camera={0};camera.forward.i=-1;
 struct render_frustum frustum={0};frustum.projection_valid=TRUE;for(int i=0;i<4;i++)frustum.projection_matrix[i][i]=1;
 if(!strcmp(case_name,"shared-local")||!strcmp(case_name,"shared-echo")||
    !strcmp(case_name,"shared-identical")||!strcmp(case_name,"shared-local-upload-failed")||
    !strcmp(case_name,"shared-remote-cache")){
  struct spray_pose pose={{0,0,0},{-1.5f,0,0},0};
  int remote=!strcmp(case_name,"shared-remote-cache");
  int local=!strcmp(case_name,"shared-local");
  int owner=remote||local?1:0;
  upload_failed=!strcmp(case_name,"shared-local-upload-failed");
  CHECK(network_spray_ready(1,owner,"x",1,&pose,"same_name",local,(const unsigned char[32]){1})==!upload_failed,"memory upload result differs");
  CHECK(saves==remote,"own spray saved as a download");
  CASE("shared-local-upload-failed"){CHECK(!shared_sprays[1].count,"failed upload replaced geometry");return 0;}
  CHECK(network_spray_ready(1,owner,"x",1,&pose,"same_name",local,(const unsigned char[32]){1}),"repeat failed");
  CHECK(saves==remote,"repeat created another download");
  CASE("shared-identical"){
   CHECK(network_spray_ready(2,1,"x",1,&pose,"same_name",0,(const unsigned char[32]){1}),"remote identical image failed");
   CHECK(saves==1,"another player's identical spray was not saved");
  }
  int before=loads;
  halo_spray_render(0,&camera,&frustum);
  CHECK(draws==(!strcmp(case_name,"shared-identical")?2:1),"memory decal not rendered");
  if(!remote&&strcmp(case_name,"shared-identical"))CHECK(loads==before,"local decal reloaded from disk");
  CHECK(saves==(remote||!strcmp(case_name,"shared-identical")),"render saved own image");return 0;
 }
 if(!strncmp(case_name,"shared",6)){
  blocked=TRUE; /* A moving object arrived while the PNG was in transit. */
  struct spray_pose pose={{0,0,0},{-1.5f,0,0},0};
  CASE("shared-bsp"){pose.bsp=1;CHECK(!network_spray_ready(1,1,"x",1,&pose,"remote",0,(const unsigned char[32]){1}),"stale BSP accepted");return 0;}
  CASE("shared-nan"){pose.direction[0]=NAN;CHECK(!network_spray_ready(1,1,"x",1,&pose,"remote",0,(const unsigned char[32]){1}),"NaN accepted");return 0;}
  CHECK(network_spray_ready(1,1,"x",1,&pose,"remote",0,(const unsigned char[32]){1})&&network_spray_ready(2,2,"y",1,&pose,"remote2",0,(const unsigned char[32]){1}),"shared placement failed");
  halo_spray_render(0,&camera,&frustum);CHECK(draws==2&&shared_sprays[1].count==6&&shared_sprays[2].count==6,"player images overwrite each other");
  halo_spray_reset();halo_spray_render(0,&camera,&frustum);CHECK(draws==2&&!shared_sprays[1].count,"shared checkpoint retained");return 0;
 }
 CASE("input"){keyboard_spray(FLAG(HALO_KEYBOARD_SPRAY),TRUE);keyboard_spray(FLAG(HALO_KEYBOARD_SPRAY),TRUE);CHECK(presses==1,"held key repeats");keyboard_spray(0,TRUE);keyboard_spray(FLAG(HALO_KEYBOARD_SPRAY),FALSE);CHECK(presses==1,"menu spray");keyboard_spray(FLAG(HALO_KEYBOARD_SPRAY),TRUE);CHECK(presses==1,"held menu key leaked into game");return 0;}
 CASE("wall") {int n=decal_build_spray_geometry(&hit,1,output,HALO_SPRAY_MAXIMUM_VERTICES);CHECK(n==6,"wall count %d",n);for(int i=0;i<n;i++){CHECK(fabs(output[i].position[0]-.0002f)<1e-6f,"not lifted from wall");CHECK(fabs(output[i].uv[1]-(.5f-2*output[i].position[2]))<1e-5f,"image vertically reversed");CHECK(fabs(output[i].uv[0]-(.5f+2*output[i].position[1]))<1e-5f,"image mirrored");}return 0;}
 CASE("edge") {wall(.1f);int n=decal_build_spray_geometry(&hit,1,output,HALO_SPRAY_MAXIMUM_VERTICES);CHECK(n==6,"edge count %d",n);for(int i=0;i<n;i++){CHECK(fabs(output[i].position[1])<=.10001f&&fabs(output[i].position[2])<=.10001f,"over wall edge");CHECK(output[i].uv[0]>.29f&&output[i].uv[0]<.71f,"clipped UV changed");}return 0;}
 CASE("aspect") {int n=decal_build_spray_geometry(&hit,2,output,HALO_SPRAY_MAXIMUM_VERTICES);CHECK(n==6,"aspect");for(int i=0;i<n;i++)CHECK(fabs(output[i].position[1])<.126f&&fabs(output[i].position[2])<.251f,"aspect stretched");return 0;}
 CASE("floor") {for(int i=0;i<4;i++){vertices[i].point.x=vertices[i].point.y;vertices[i].point.y=vertices[i].point.z;vertices[i].point.z=0;}bsp.bsp3d.plane.n.i=0;bsp.bsp3d.plane.n.k=1;hit.plane=bsp.bsp3d.plane;int n=decal_build_spray_geometry(&hit,1,output,HALO_SPRAY_MAXIMUM_VERTICES);CHECK(n==6,"floor %d",n);for(int i=0;i<n;i++)CHECK(isfinite(output[i].uv[0]),"floor singularity");return 0;}
 CASE("invisible") {surfaces[0].flags=FLAG(_collision_surface_invisible_bit);CHECK(!decal_build_spray_geometry(&hit,1,output,HALO_SPRAY_MAXIMUM_VERTICES),"invisible surface");return 0;}
 CASE("broken-ring") {edges[2].vertex_indices[0]=500;CHECK(!decal_build_spray_geometry(&hit,1,output,HALO_SPRAY_MAXIMUM_VERTICES),"invalid vertices accepted");return 0;}
 CASE("capacity") {CHECK(!decal_build_spray_geometry(&hit,1,output,3),"over capacity");CHECK(!decal_build_spray_geometry(&hit,NAN,output,3072),"NaN accepted");return 0;}
 CASE("cooldown") {
  request=TRUE;halo_spray_render(0,&camera,&frustum);CHECK(sounds==1,"first sound");
  for(tick=0;tick<120;tick++){request=TRUE;halo_spray_render(0,&camera,&frustum);CHECK(!request&&loads==1&&sounds==1&&rays==1,"cooldown admitted at %ld",tick);}
  choice=1;request=TRUE;halo_spray_render(0,&camera,&frustum);CHECK(loads==2&&sounds==2,"four second boundary");
  halo_spray_reset();request=TRUE;halo_spray_render(0,&camera,&frustum);CHECK(sounds==3,"reset retained cooldown");return 0;
 }
 CASE("failed-cooldown") {
  blocked=TRUE;request=TRUE;halo_spray_render(0,&camera,&frustum);CHECK(sounds==0,"miss played sound");
  blocked=FALSE;surfaces[0].flags=FLAG(_collision_surface_invisible_bit);request=TRUE;halo_spray_render(0,&camera,&frustum);CHECK(sounds==0,"invalid surface played sound");
  surfaces[0].flags=0;request=TRUE;halo_spray_render(0,&camera,&frustum);CHECK(sounds==1&&spray_vertex_count==6,"failed attempt consumed cooldown");return 0;
 }
 CASE("preparing"){
  source_preparing=TRUE;request=TRUE;halo_spray_render(0,&camera,&frustum);CHECK(spray_pending&&!sounds&&!loads,"preparation blocked or consumed a placement");
  camera.position.x=5;source_preparing=FALSE;halo_spray_render(0,&camera,&frustum);CHECK(!spray_pending&&sounds==1&&loads==1,"prepared request lost");return 0;
 }
 CASE("preparing-reset"){
  source_preparing=TRUE;request=TRUE;halo_spray_render(0,&camera,&frustum);halo_spray_reset();source_preparing=FALSE;halo_spray_render(0,&camera,&frustum);CHECK(!sounds&&!loads,"reset retained pending placement");return 0;
 }
 CASE("network") connection=_game_connection_network_server;
 CASE("multiplayer") connection=_game_connection_network_server;
 CASE("split-screen") players=2;
 CASE("menu") menu=TRUE;
 CASE("pause") paused=TRUE;
 CASE("cinematic") cinematic=TRUE;
 CASE("dead") unit=NONE;
 CASE("blocked") blocked=TRUE;
 CASE("missing-image") missing=TRUE;
 request=TRUE;halo_spray_render(0,&camera,&frustum);CHECK(!request,"unconsumed input");
 if(!strcmp(case_name,"singleplayer")||!strcmp(case_name,"network")||!strcmp(case_name,"multiplayer")||!strcmp(case_name,"replace")||!strcmp(case_name,"reset")){
  CHECK(loads==1&&draws==1&&spray_vertex_count==6,"placement failed");
  CASE("replace"){tick=120;request=TRUE;halo_spray_render(0,&camera,&frustum);CHECK(loads==2&&draws==2&&spray_vertex_count==6,"sprays accumulated");}
  CASE("reset"){request=TRUE;halo_spray_reset();halo_spray_render(0,&camera,&frustum);CHECK(!request&&!spray_vertex_count&&draws==1&&forgotten==1,"checkpoint retained spray");}
 }else CHECK(!sounds&&!draws&&!spray_vertex_count,"spray admitted in %s",case_name);
 return 0;
}
