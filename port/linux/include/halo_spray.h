/* Image decals. All storage is outside the saved game state. */
#ifndef HALO_SPRAY_H
#define HALO_SPRAY_H

#define HALO_SPRAY_MAXIMUM_VERTICES 3072
#include "spray_share.h"

struct halo_spray_vertex
{
	float position[3];
	float uv[2];
};

struct halo_spray_clip_vertex
{
	float position[4];
	float uv[2];
};

struct render_camera;
struct render_frustum;
struct collision_result;

void halo_spray_reset(void);
void platform_spray_request(void);
int platform_spray_take_request(void);
void halo_spray_render(short local_player_index, struct render_camera const *camera,
	struct render_frustum const *frustum);
int decal_build_spray_geometry(struct collision_result const *collision, float aspect,
	struct halo_spray_vertex *vertices, int capacity);

/* Prepared source snapshots are immutable and owned by the native worker. */
struct halo_spray_source
{
 const void *data;
 size_t size;
 unsigned char hash[32];
 float aspect;
 void *prepared;
};
int halo_spray_source_acquire(struct halo_spray_source *source); /* -1 preparing, 0 absent/invalid, 1 ready */
void halo_spray_source_release(struct halo_spray_source *source);
int halo_spray_image_enqueue(int slot, const void *data, size_t size, const unsigned char hash[32], const char *name, int save);
int spray_image_enqueue_local(float *aspect);
void spray_images_update(void);
void halo_spray_images_update(void);
void halo_spray_worker_shutdown(void);

/* Native renderer: independent image slots, with bounded GPU residency. */
int halo_spray_image_load(float *aspect);
void halo_spray_image_forget(void);
void halo_spray_image_draw(struct halo_spray_clip_vertex const *vertices, int count);
void halo_spray_draw(struct halo_spray_clip_vertex const *vertices, int count);
int spray_image_load(float *aspect);
int halo_spray_image_load_slot(int slot, const char *path, float *aspect);
int spray_image_load_slot(int slot, const char *path, float *aspect);
int halo_spray_image_load_bytes(int slot, const void *data, size_t size, float *aspect);
int spray_image_load_bytes(int slot, const void *data, size_t size, float *aspect);
void halo_spray_image_draw_slot(int slot, struct halo_spray_clip_vertex const *vertices, int count);
void halo_spray_draw_slot(int slot, struct halo_spray_clip_vertex const *vertices, int count);
int halo_spray_file_read(void **data, size_t *size);
/* Release file buffers in the native loader's allocator, outside Halo's heap. */
void halo_spray_file_free(void *data);
int halo_spray_file_save(const void *data, size_t size, const char *name,
 char *path, size_t capacity, float *aspect);
int halo_spray_image_aspect(const void *data, size_t size, float *aspect);
int halo_spray_file_cached(const unsigned char hash[32], size_t size, void *data);
void network_spray_update(void);
int network_spray_handles_message(const void *message, unsigned short size);
void network_spray_handle_message(long machine, const void *message, unsigned short size);
void network_spray_machine_joined(long machine);
void network_spray_reset(void);
int network_spray_publish(struct spray_pose pose);
int network_spray_is_local(int owner);
long network_spray_unit(int machine);
int network_spray_ready(int slot, int owner, const void *data, size_t size,
 const struct spray_pose *pose, const char *name, int local, const unsigned char hash[32]);

#endif
