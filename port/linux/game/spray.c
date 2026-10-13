#include "cseries.h"
#include "halo_spray.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/ui_widget.h"
#include "cutscene/cinematics.h"
#include "physics/collisions.h"
#include "render/render_cameras.h"
#include "sound/sound_manager.h"
#include "sound/sound_definitions.h"
#include "scenario/scenario.h"
#include "../../third_party/monocypher/monocypher.h"
#include <math.h>
#include <string.h>

static struct halo_spray_vertex spray_vertices[HALO_SPRAY_MAXIMUM_VERTICES];
static int spray_vertex_count;
static boolean spray_cooldown_active;
static unsigned long spray_last_tick;
static struct {
 struct halo_spray_vertex vertices[HALO_SPRAY_MAXIMUM_VERTICES];
 int count,dirty;char path[4096];unsigned char hash[32];
} shared_sprays[SPRAY_SHARE_SLOTS];

int network_spray_ready(int slot,int owner,const void *data,size_t size,
 const struct spray_pose *pose,const char *name,int local)
{
 struct collision_result collision;real_point3d origin;real_vector3d direction;
 float aspect,length=0;unsigned char hash[32];int i,count;
 static struct halo_spray_vertex vertices[HALO_SPRAY_MAXIMUM_VERTICES];
 if(slot<0||slot>=SPRAY_SHARE_SLOTS||pose->bsp!=global_structure_bsp_index_get())return 0;
 for(i=0;i<3;i++){
  if(!isfinite(pose->origin[i])||fabsf(pose->origin[i])>32768||!isfinite(pose->direction[i]))return 0;
  origin.n[i]=pose->origin[i];direction.n[i]=pose->direction[i];length+=direction.n[i]*direction.n[i];
 }
 if(length<2.249f||length>2.251f||!halo_spray_image_aspect(data,size,&aspect)||
  /* The host already checked moving blockers at the time of placement.
   * A transfer can finish later: project onto the static wall, regardless of
   * where players have since moved, died or respawned. */
  !collision_test_vector(FLAG(_collision_test_front_facing_surfaces_bit)|FLAG(_collision_test_structure_bit),
   &origin,&direction,NONE,&collision)||collision.type!=_collision_result_structure)return 0;
 count=decal_build_spray_geometry(&collision,aspect,vertices,HALO_SPRAY_MAXIMUM_VERTICES);if(!count)return 0;
 crypto_blake2b(hash,32,data,size);
 /* Local publication and host echoes never create a downloaded copy. Use the
  * published bytes so changing spray.png cannot change an existing decal. */
 if(local||network_spray_is_local(owner)){
  if(!halo_spray_image_load_bytes(slot,data,size,&aspect))return 0;
  shared_sprays[slot].path[0]=0;shared_sprays[slot].dirty=0;
  memcpy(shared_sprays[slot].hash,hash,32);
 }else if(!shared_sprays[slot].path[0]||memcmp(hash,shared_sprays[slot].hash,32)){
  char path[4096];if(!halo_spray_file_save(data,size,name,path,sizeof(path),&aspect))return 0;
  memcpy(shared_sprays[slot].path,path,strlen(path)+1);memcpy(shared_sprays[slot].hash,hash,32);shared_sprays[slot].dirty=1;
 }
 memcpy(shared_sprays[slot].vertices,vertices,(size_t)count*sizeof(*vertices));shared_sprays[slot].count=count;return 1;
}

void halo_spray_reset(void)
{
	spray_vertex_count = 0;
	spray_cooldown_active = FALSE;
	platform_spray_take_request();
	network_spray_reset();
	memset(shared_sprays,0,sizeof(shared_sprays));
	halo_spray_image_forget();
}

static boolean spray_allowed(short local_player_index)
{
	return local_player_index == 0 && game_in_progress() && !main_menu_is_active() &&
		local_player_count() == 1;
}

static void spray_project(struct render_frustum const *frustum,
	struct halo_spray_vertex const *in, struct halo_spray_clip_vertex *out)
{
	real_point3d world, view;
	int column;
	world.x = in->position[0]; world.y = in->position[1]; world.z = in->position[2];
	matrix4x3_transform_point(&frustum->world_to_view, &world, &view);
	for (column = 0; column < 4; column++)
		out->position[column] = view.x * frustum->projection_matrix[0][column] +
			view.y * frustum->projection_matrix[1][column] +
			view.z * frustum->projection_matrix[2][column] +
			frustum->projection_matrix[3][column];
	out->uv[0] = in->uv[0]; out->uv[1] = in->uv[1];
}

void halo_spray_render(short local_player_index, struct render_camera const *camera,
	struct render_frustum const *frustum)
{
	static struct halo_spray_clip_vertex projected[HALO_SPRAY_MAXIMUM_VERTICES];
	boolean requested = platform_spray_take_request();
	int index;
	if (!spray_allowed(local_player_index) || !frustum->projection_valid)
		return;
	if (requested && !game_time_get_paused() && !cinematic_in_progress() &&
		(!spray_cooldown_active ||
		 (unsigned long)game_time_get() - spray_last_tick >= 4 * TICKS_PER_SECOND))
	{
		long unit_index = player_control_get_unit_index(0);
		struct collision_result collision;
		real_vector3d direction;
		float aspect;
		/* An object between the player and a wall blocks the spray too. */
		direction.i = camera->forward.i * 1.5f;
		direction.j = camera->forward.j * 1.5f;
		direction.k = camera->forward.k * 1.5f;
		if (unit_index != NONE && collision_test_vector(
			FLAG(_collision_test_front_facing_surfaces_bit) |
				_collision_test_environment_flags | _collision_test_objects_all_types_flags,
			&camera->position, &direction, unit_index, &collision) &&
			collision.type == _collision_result_structure)
		{
			struct spray_pose pose;boolean placed=FALSE;
			memcpy(pose.origin,&camera->position,sizeof(pose.origin));memcpy(pose.direction,&direction,sizeof(pose.direction));
			pose.bsp=global_structure_bsp_index_get();
			if(game_connection()!=_game_connection_local&&network_spray_publish(pose))
			{spray_vertex_count=0;placed=TRUE;}
			else if(halo_spray_image_load(&aspect)){
			 spray_vertex_count = decal_build_spray_geometry(&collision, aspect,spray_vertices,HALO_SPRAY_MAXIMUM_VERTICES);
			 placed=spray_vertex_count!=0;
			}
			if (placed)
			{
				long sound_index = tag_loaded(SOUND_DEFINITION_TAG,
					"sound\\sfx\\weapons\\plasma rifle\\overheat");
				spray_last_tick = (unsigned long)game_time_get();
				spray_cooldown_active = TRUE;
				if (sound_index != NONE)
					unspatialized_impulse_sound_new_named(sound_index,
						local_random_range(0, 2) ? "overheat3" : "overheat2");
			}
		}
	}
	for (index = 0; index < spray_vertex_count; index++)
		spray_project(frustum, &spray_vertices[index], &projected[index]);
	if (spray_vertex_count)
		halo_spray_draw(projected, spray_vertex_count);
	for(index=0;index<SPRAY_SHARE_SLOTS;index++)if(shared_sprays[index].count){
	 int vertex;float aspect;
	 if(shared_sprays[index].dirty){
	  if(!halo_spray_image_load_slot(index,shared_sprays[index].path,&aspect))continue;
	  shared_sprays[index].dirty=0;
	 }
	 for(vertex=0;vertex<shared_sprays[index].count;vertex++)spray_project(frustum,&shared_sprays[index].vertices[vertex],&projected[vertex]);
	 halo_spray_draw_slot(index,projected,shared_sprays[index].count);
	}
}
