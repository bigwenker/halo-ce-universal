/* Loose cosmetic models. This interface contains no platform-owned handles. */
#ifndef HALO_CUSTOM_MODELS_H
#define HALO_CUSTOM_MODELS_H
enum { HALO_CUSTOM_MODELS = 64, HALO_CUSTOM_MODEL_ID = 32, HALO_CUSTOM_MODEL_NODES = 19 };
struct halo_custom_model_info { char id[32], name[96], preview[128]; int colorable; char default_rgb[8]; };
struct halo_custom_node { char name[32]; int child, sibling, parent; float rotation[4], translation[3]; };
struct halo_custom_vertex { float position[3], normal[3], uv[2], weight; int node[2]; };
struct halo_custom_mesh
{
    struct halo_custom_node nodes[19];
    int vertex_count, triangle_count;
    struct halo_custom_vertex *vertices;
    unsigned short *triangles; /* native model winding, converted from JMS */
};
struct halo_custom_image { int width, height; unsigned int *pixels; };
int halo_custom_model_id_valid(char const *id);
void halo_custom_models_scan(void);
int halo_custom_models_count(void);
struct halo_custom_model_info const *halo_custom_model_info(int index);
int halo_custom_model_find(char const *id);
int halo_custom_mesh_parse(char const *data, unsigned int size, struct halo_custom_mesh *mesh);
int halo_custom_mesh_load(int index, int lod, struct halo_custom_mesh *mesh);
void halo_custom_mesh_dispose(struct halo_custom_mesh *mesh);
int halo_custom_image_load(int index, int kind, struct halo_custom_image *image);
void halo_custom_image_dispose(struct halo_custom_image *image);
#endif
