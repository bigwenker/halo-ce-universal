/* Render-only humanoid retargeting. Matrices use Halo's column basis layout. */
#ifndef HALO_CUSTOM_RETARGET_H
#define HALO_CUSTOM_RETARGET_H
#include "halo_custom_models.h"
enum { HALO_RETARGET_VERSION = 3, HALO_RETARGET_CACHE_BYTES = 16384 };
struct halo_custom_matrix { float scale, v[4][3]; };
struct halo_custom_retarget
{
    int parent[HALO_CUSTOM_MAX_NODES], map[HALO_CUSTOM_MAX_NODES], identical, count;
    struct halo_custom_matrix source_inverse[HALO_CUSTOM_MAX_NODES];
    struct halo_custom_matrix target[HALO_CUSTOM_MAX_NODES];
    struct halo_custom_matrix target_inverse[HALO_CUSTOM_MAX_NODES];
    /* Source joint axes to target joint axes, calibrated from limb directions. */
    struct halo_custom_matrix rotation_map[HALO_CUSTOM_MAX_NODES];
};
int halo_custom_skeleton_hash_count(int count,struct halo_custom_node const *source,struct halo_custom_node const *target,unsigned char hash[32]);
int halo_custom_retarget_build_count(int count,struct halo_custom_node const *source,struct halo_custom_node const *target,struct halo_custom_retarget *plan);
int halo_custom_skeleton_hash(struct halo_custom_node const *source,struct halo_custom_node const *target,unsigned char hash[32]);
int halo_custom_retarget_build(struct halo_custom_node const *source,struct halo_custom_node const *target,struct halo_custom_retarget *plan);
int halo_custom_retarget_pose(struct halo_custom_retarget const *plan,struct halo_custom_matrix const *source,struct halo_custom_matrix *target);
int halo_custom_retarget_arm(struct halo_custom_matrix *shoulder,struct halo_custom_matrix *elbow,struct halo_custom_matrix *hand,struct halo_custom_matrix const *goal);
int halo_custom_retarget_grip(struct halo_custom_retarget const *plan,struct halo_custom_matrix const *source,struct halo_custom_matrix *target,int upper,int forearm,int wrist);
int halo_custom_retarget_contact(struct halo_custom_retarget const *plan,struct halo_custom_matrix const *source,struct halo_custom_matrix *target,int clavicle,int upper,int forearm,int wrist,struct halo_custom_matrix const *goal);
void halo_custom_matrix_multiply(struct halo_custom_matrix const *a,struct halo_custom_matrix const *b,struct halo_custom_matrix *out);
void halo_custom_matrix_inverse(struct halo_custom_matrix const *a,struct halo_custom_matrix *out);
void halo_custom_matrix_node(struct halo_custom_node const *node,struct halo_custom_matrix *out);
int halo_custom_retarget_cache_load(int index,int first_person,unsigned char const key[32],struct halo_custom_retarget *plan);
void halo_custom_retarget_cache_save(int index,int first_person,unsigned char const key[32],struct halo_custom_retarget const *plan);
int halo_custom_retarget_encode(struct halo_custom_retarget const *plan,unsigned char const key[32],unsigned char data[HALO_RETARGET_CACHE_BYTES]);
int halo_custom_retarget_decode(unsigned char const *data,unsigned int size,unsigned char const key[32],struct halo_custom_retarget *plan);
#endif
