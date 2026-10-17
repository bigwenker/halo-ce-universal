#include "harness.h"

typedef unsigned short word;
typedef struct { real i,j; } real_vector2d;
typedef struct { real x,y,z,w; } real_vector4d;
struct shader { struct { short type; } base; };
struct shader_model_definition {
    struct shader shader; word flags; real_vector2d map_scale;
    struct { long index; } base_map;
    real detail_map_scale,detail_map_v_scale; int animation;
};
struct triangle_buffer { long count; };
struct vertex_buffer { int type; };
struct { int rasterizer_target; } global_window_parameters;
struct { int draw_environment_shadows,statistics_mode; } rasterizer_debug_options;
struct { int model_shadow_draw_count,model_shadow_triangle_count,model_shadow_vertex_count; } rasterizer_frame_statistics;
static struct { int animation;real_vector2d base_map_scale; } parameters,*local_parameters=&parameters;
static int global_d3d_device=1;
enum { _rasterizer_target_render_primary=0,_shader_type_model=4,
    _rasterizer_statistics_mode_enabled=2,_shadow_model_vertex_shader_index=39,
    D3DRS_CULLMODE,D3DRS_PSTEXTUREMODES,D3DRS_ALPHATESTENABLE,
    D3DCULL_NONE,D3DCULL_CCW,D3DTSS_ADDRESSU,D3DTSS_ADDRESSV,D3DTSS_MAGFILTER,
    D3DTSS_MINFILTER,D3DTSS_MIPFILTER,D3DTADDRESS_WRAP,D3DTEXF_LINEAR };
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
#include "flags.inc"
static int states[64],draws,covered,tex_alpha=255,samples;
static struct shader *shader_get_and_verify_type(struct shader *shader,int type) { (void)type;return shader; }
static void IDirect3DDevice8_SetRenderState(int device,int state,int value) { (void)device;states[state]=value; }
static void rasterizer_set_vertex_shader_permutation(int shader,int type,int permutation) { (void)shader;(void)type;(void)permutation; }
static void rasterizer_set_texture(int stage,int type,int usage,long index,int bitmap) { (void)stage;(void)type;(void)usage;(void)index;(void)bitmap;samples++; }
#define IDirect3DDevice8_SetTextureStageState(...) ((void)0)
#define shader_texture_animation_evaluate(...) ((void)0)
#define IDirect3DDevice8_SetVertexShaderConstant(...) ((void)0)
#define rasterizer_frame_statistics_count_static_vertices(...) 3
static void rasterizer_draw_static_triangles_static_vertices(struct triangle_buffer const *t,long first,long count,struct vertex_buffer const *v)
{
    int alpha=states[D3DRS_PSTEXTUREMODES] ? tex_alpha : 0;
    (void)t;(void)first;(void)count;(void)v;draws++;
    if(!states[D3DRS_ALPHATESTENABLE] || alpha>=128)covered++;
}
#include "under_test.inc"
int main(int argc,char **argv)
{
    struct shader_model_definition model={0};
    struct triangle_buffer triangles={1};struct vertex_buffer vertices={0};
    CHECK(argc==2,"case");
    model.shader.base.type=_shader_type_model;rasterizer_debug_options.draw_environment_shadows=TRUE;
    states[D3DRS_ALPHATESTENABLE]=TRUE;
    if(!strcmp(argv[1],"opaque"))
    {
        model.flags=FLAG(_shader_model_not_alpha_tested_bit);
        _rasterizer_environment_shadow_model_draw(&model.shader,0,&triangles,&vertices);
        CHECK(covered==1 && !samples,"opaque shadow discarded or sampled an alpha texture");
        CHECK(states[D3DRS_CULLMODE]==D3DCULL_CCW,"opaque winding changed");
    }
    else if(!strcmp(argv[1],"cutout-solid") || !strcmp(argv[1],"cutout-hole"))
    {
        tex_alpha=!strcmp(argv[1],"cutout-hole") ? 0 : 255;
        _rasterizer_environment_shadow_model_draw(&model.shader,0,&triangles,&vertices);
        CHECK(states[D3DRS_ALPHATESTENABLE] && samples==1,"cutout coverage disabled");
        CHECK(covered==(tex_alpha!=0),"wrong cutout shadow coverage");
    }
    else if(!strcmp(argv[1],"mixed"))
    {
        model.flags=FLAG(_shader_model_not_alpha_tested_bit);
        _rasterizer_environment_shadow_model_draw(&model.shader,0,&triangles,&vertices);
        CHECK(covered==1,"first opaque part missing");
        model.flags=0;tex_alpha=0;
        _rasterizer_environment_shadow_model_draw(&model.shader,0,&triangles,&vertices);
        CHECK(covered==1 && states[D3DRS_ALPHATESTENABLE],"opaque state leaked into cutout part");
        model.flags=FLAG(_shader_model_not_alpha_tested_bit);
        _rasterizer_environment_shadow_model_draw(&model.shader,0,&triangles,&vertices);
        CHECK(covered==2 && !states[D3DRS_ALPHATESTENABLE],"cutout state leaked into opaque part");
    }
    else if(!strcmp(argv[1],"disabled") || !strcmp(argv[1],"other-target") || !strcmp(argv[1],"other-shader"))
    {
        if(!strcmp(argv[1],"disabled"))rasterizer_debug_options.draw_environment_shadows=FALSE;
        if(!strcmp(argv[1],"other-target"))global_window_parameters.rasterizer_target=1;
        if(!strcmp(argv[1],"other-shader"))model.shader.base.type=7;
        _rasterizer_environment_shadow_model_draw(&model.shader,0,&triangles,&vertices);
        CHECK(!draws && !samples && states[D3DRS_ALPHATESTENABLE],"unrelated pass altered");
    }
    else if(!strcmp(argv[1],"two-sided"))
    {
        model.flags=FLAG(_shader_model_two_sided_bit);
        _rasterizer_environment_shadow_model_draw(&model.shader,0,&triangles,&vertices);
        CHECK(covered==1 && states[D3DRS_CULLMODE]==D3DCULL_NONE,"two-sided shadow changed");
    }
    else CHECK(FALSE,"unknown case");
    return 0;
}
