/* Per-object cosmetic models built as ordinary model, shader and bitmap tags.
   The original unit, animation graph, collision and replicated definition
   remain intact. Chief's validated node order drives every replacement. */
#include "cseries.h"
#include "custom_models.h"
#include "halo_custom_models.h"
#include "player_profile_rgb.h"
#include "models/model_definitions.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_model_types.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "game/players.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "saved games/player_profile.h"
#include "tag_files/files.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances,long count);
void platform_log(char const *format,...);

/* The runtime layouts also used in models.c and custom_edition_geometry.c. */
struct custom_tag_instance { long group_tag, parents[2], tag_index; char *name; void *base_address; unsigned long unused[2]; };
struct custom_shader_ref { struct tag_reference shader; short permutation,pad; long unused[3]; };
struct custom_geometry { byte reserved[0x24]; struct tag_block parts; };
struct custom_part
{
    unsigned long flags; short shader; char previous,next;
    short centroid_node[2]; real centroid_weight[2]; real_point3d centroid;
    struct tag_block uncompressed,compressed,triangles;
    struct triangle_buffer triangle_buffer; struct vertex_buffer vertex_buffer;
};
/* Model shader layout recovered in rasterizer_xbox_models.c. */
struct custom_shader
{
    byte base[0x28]; word flags; short type; byte reserved04[12]; real translucency;
    byte reserved14[16]; short color_source; byte reserved26[30];
    word illumination_flags;short pad46,illumination_source,illumination_function;
    real illumination_period;real_rgb_color illumination_lower,illumination_upper;
    byte reserved68[12];real u_scale,v_scale;
    struct tag_reference base_map;byte reserved8c[8];struct tag_reference multipurpose;
    byte reserveda4[8];short detail_function,detail_mask;real detail_scale;
    struct tag_reference detail;real detail_v_scale;byte reservedc8[12];
    byte animation[0x38];byte reserved10c[8];
    real reflection_falloff,reflection_cutoff,perpendicular_brightness;real_rgb_color perpendicular_tint;
    real parallel_brightness;real_rgb_color parallel_tint;struct tag_reference cubemap;
};
typedef char custom_part_size[sizeof(struct custom_part)==0x68 ? 1:-1];
typedef char custom_shader_base_offset[offsetof(struct custom_shader,base_map.index)==0xB0 ? 1:-1];
typedef char custom_shader_size[sizeof(struct custom_shader)==0x174 ? 1:-1];
struct custom_asset
{
    struct model model;
    struct custom_shader shader;
    struct custom_shader_ref shader_ref;
    struct custom_geometry geometry[5];struct custom_part parts[5];
    struct model_region region;struct model_region_permutation permutation;
    struct bitmap_group bitmap_groups[2];struct bitmap_group_sequence sequences[2];
    struct bitmap_data *bitmaps[2];
    void *vertex_data[5],*index_data[5];
    char names[4][128],id[32];long tag;
    boolean ready;unsigned long memory_bytes;
};
static struct custom_asset assets[HALO_CUSTOM_MODELS];
static struct custom_tag_instance *original_table,*new_table;
static long original_count, stock_model=NONE;
static struct {long handle;long tag;} objects[HALO_PORT_MAXIMUM_OBJECTS_PER_MAP];
static struct bitmap_data *preview_bitmap;
static short preview_model=NONE,preview_loaded=NONE;
static unsigned long geometry_bytes;

static void *zero_allocate(long bytes)
{
    void *memory=malloc(bytes);
    if(memory)memset(memory,0,bytes);
    return memory;
}
static boolean profile_file(long profile,struct file_reference *file)
{
    char directory[256],path[300];
    if(profile==NONE || !player_profile_get_enclosing_directory_path(profile,directory))return FALSE;
    if(snprintf(path,sizeof(path),"%sprofile_model.txt",directory)>=sizeof(path))return FALSE;
    return file_reference_create_from_path(file,path,FALSE)!=NULL;
}
boolean custom_models_default_rgb(char const *id,unsigned long *rgb)
{
    struct halo_custom_model_info const *info=halo_custom_model_info(halo_custom_model_find(id));
    return info && info->colorable && profile_rgb_parse(info->default_rgb,rgb);
}
boolean custom_models_profile_default_rgb(long profile,unsigned long *rgb)
{
    char id[32];
    return custom_models_profile_read(profile,id) && custom_models_default_rgb(id,rgb);
}
boolean custom_models_profile_read(long profile,char id[32])
{
    struct file_reference file;long size;boolean ok=FALSE;
    strcpy(id,"master_chief");
    if(!profile_file(profile,&file) || !file_open(&file,FLAG(_permission_read_bit)))return FALSE;
    size=file_get_eof(&file);
    if(size>0 && size<32 && file_read(&file,size,id))
    {
        id[size]=0;
        if(halo_custom_model_id_valid(id) && (long)strlen(id)==size)ok=TRUE;
    }
    file_close(&file);
    if(!ok)strcpy(id,"master_chief");
    return ok;
}
boolean custom_models_profile_write(long profile,char const *id)
{
    struct file_reference file;boolean ok;long size=(long)strlen(id);
    if(!size || !halo_custom_model_id_valid(id) || !profile_file(profile,&file) ||
        (!file_exists(&file) && !file_create(&file)) || !file_open(&file,FLAG(_permission_write_bit)))return FALSE;
    ok=file_write(&file,size,id) && file_set_eof(&file,size);
    if(!file_close(&file))ok=FALSE;
    if(ok)custom_models_selection_changed();
    return ok;
}
static struct bitmap_data *image_bitmap(short index,short kind)
{
    struct halo_custom_image image;
    struct bitmap_data *bitmap;
    if(!halo_custom_image_load(index,kind,&image))return NULL;
    bitmap=bitmap_2d_new((short)image.width,(short)image.height,0,11);
    if(bitmap && bitmap->base_address)
    {memcpy(bitmap->base_address,image.pixels,image.width*image.height*4);bitmap_rebuild(bitmap);}
    halo_custom_image_dispose(&image);
    return bitmap;
}
void custom_models_preview_forget(void)
{
    if(preview_bitmap)bitmap_delete(preview_bitmap);
    preview_bitmap=NULL;preview_loaded=NONE;preview_model=NONE;
}
void custom_models_menu_preview(short index) {preview_model=index;}
struct bitmap_data *custom_models_preview_bitmap(char const *name,struct bitmap_data *original)
{
    if(strcmp(name,"custom_model_preview"))return original;
    if(preview_model!=preview_loaded)
    {
        if(preview_bitmap)bitmap_delete(preview_bitmap);
        preview_bitmap=preview_model>0 ? image_bitmap(preview_model,0) : NULL;
        preview_loaded=preview_model;
    }
    return preview_bitmap ? preview_bitmap : original;
}
static boolean skeleton_matches(struct halo_custom_mesh const *mesh,struct model const *stock)
{
    short i,j;
    if(stock->nodes.count!=19)return FALSE;
    for(i=0;i<19;i++)
    {
        struct model_node const *node=(struct model_node const *)stock->nodes.address+i;
        struct halo_custom_node const *other=&mesh->nodes[i];
        real const *translation=&node->default_translation.x;
        real const *rotation=&node->default_rotation.v.i;
        real dot=0;
        if(strcmp(node->name,other->name) || node->parent_node_index!=other->parent)return FALSE;
        for(j=0;j<3;j++)if(fabs(translation[j]-other->translation[j])>.001f)return FALSE;
        for(j=0;j<4;j++)dot+=rotation[j]*other->rotation[j];
        if(fabs(dot)<.9999f)return FALSE;
    }
    return TRUE;
}
static boolean geometry_make(struct custom_asset *asset,short index,short lod,struct model *stock)
{
    struct halo_custom_mesh mesh;
    struct model_vertex_uncompressed *vertices;
    struct model_vertex_compressed *compressed;
    struct custom_part *part=&asset->parts[lod];
    long i;unsigned long bytes;
    if(!halo_custom_mesh_load(index,lod,&mesh))return FALSE;
    bytes=3*(mesh.vertex_count*sizeof(*compressed)+mesh.triangle_count*6);
    if(!skeleton_matches(&mesh,stock) || bytes>0x8000000UL-geometry_bytes){halo_custom_mesh_dispose(&mesh);return FALSE;}
    vertices=zero_allocate(mesh.vertex_count*sizeof(*vertices));compressed=malloc(mesh.vertex_count*sizeof(*compressed));
    if(!vertices || !compressed){if(vertices)free(vertices);if(compressed)free(compressed);halo_custom_mesh_dispose(&mesh);return FALSE;}
    for(i=0;i<mesh.vertex_count;i++)
    {
        struct halo_custom_vertex *v=&mesh.vertices[i];
        struct model_vertex_uncompressed *out=&vertices[i];
        real length;
        memcpy(&out->position,v->position,12);memcpy(&out->normal,v->normal,12);
        out->texcoord.x=v->uv[0];out->texcoord.y=v->uv[1];
        out->nodes[0]=(short)v->node[0];out->nodes[1]=(short)(v->node[1]<0 ? v->node[0] : v->node[1]);
        out->node_weights[0]=1.f-v->weight;out->node_weights[1]=v->weight;
        if(fabs(out->normal.k)<.9f){out->tangent.i=-out->normal.j;out->tangent.j=out->normal.i;}
        else {out->tangent.i=out->normal.k;out->tangent.k=-out->normal.i;}
        length=sqrtf(out->tangent.i*out->tangent.i+out->tangent.j*out->tangent.j+out->tangent.k*out->tangent.k);
        out->tangent.i/=length;out->tangent.j/=length;out->tangent.k/=length;
        cross_product3d(&out->normal,&out->tangent,&out->binormal);
    }
    rasterizer_geometry_compress_vertices(_rasterizer_vertex_type_model_uncompressed,mesh.vertex_count,compressed,
        mesh.vertex_count*sizeof(*compressed),vertices,mesh.vertex_count*sizeof(*vertices));
    part->previous=part->next=NONE;part->centroid_weight[0]=1.f;
    asset->geometry[lod].parts.count=1;asset->geometry[lod].parts.address=part;
    asset->vertex_data[lod]=compressed;
    asset->index_data[lod]=malloc(mesh.triangle_count*6);
    if(!asset->index_data[lod]){free(vertices);halo_custom_mesh_dispose(&mesh);return FALSE;}
    memcpy(asset->index_data[lod],mesh.triangles,mesh.triangle_count*6);
    if(!rasterizer_vertex_buffer_new(&part->vertex_buffer,_rasterizer_vertex_type_model_compressed,mesh.vertex_count,compressed,mesh.vertex_count*sizeof(*compressed)) ||
        !rasterizer_triangle_buffer_new(&part->triangle_buffer,_triangle_buffer_type_triangles,mesh.triangle_count,asset->index_data[lod]))
    {free(vertices);halo_custom_mesh_dispose(&mesh);return FALSE;}
    geometry_bytes+=bytes;asset->memory_bytes+=bytes;
    free(vertices);halo_custom_mesh_dispose(&mesh);return TRUE;
}
static void asset_dispose(struct custom_asset *asset)
{
    short i;
    for(i=0;i<5;i++)
    {
        if(asset->parts[i].vertex_buffer.hardware_format)rasterizer_vertex_buffer_delete(&asset->parts[i].vertex_buffer);
        if(asset->parts[i].triangle_buffer.hardware_format)rasterizer_triangle_buffer_delete(&asset->parts[i].triangle_buffer);
        if(asset->vertex_data[i])free(asset->vertex_data[i]);if(asset->index_data[i])free(asset->index_data[i]);
    }
    for(i=0;i<2;i++)if(asset->bitmaps[i])bitmap_delete(asset->bitmaps[i]);
    geometry_bytes-=asset->memory_bytes;
    memset(asset,0,sizeof(*asset));
}
static void tag_set(long at,long group,void *definition,char *name)
{
    struct custom_tag_instance *instance=&new_table[at];
    memset(instance,0,sizeof(*instance));instance->group_tag=group;
    instance->parents[0]=group=='soso' ? 'shdr' : NONE;instance->parents[1]=NONE;
    instance->tag_index=0x6F000000L|at;instance->name=name;instance->base_address=definition;
}
static void reference_set(struct tag_reference *reference,long group,long index)
{memset(reference,0,sizeof(*reference));reference->group_tag=group;reference->index=index;reference->name="";}
static boolean asset_make(short index,struct model *stock,long first)
{
    struct custom_asset *asset=&assets[index];short i;long stock_shader;
    struct halo_custom_model_info const *info=halo_custom_model_info(index);
    stock_shader=tag_loaded('soso',"characters\\cyborg\\shaders\\armor");
    if(stock_shader==NONE)stock_shader=tag_loaded('soso',"characters\\cyborg_mp\\shaders\\armor");
    if(stock_shader==NONE)return FALSE;
    asset->bitmaps[0]=image_bitmap(index,1);
    if(!asset->bitmaps[0])return FALSE;
    if(info->colorable)asset->bitmaps[1]=image_bitmap(index,2);
    if(!asset->bitmaps[1])
    {
        asset->bitmaps[1]=bitmap_2d_new(4,4,0,11);
        if(!asset->bitmaps[1])return FALSE;
        for(i=0;i<16;i++)((pixel32 *)asset->bitmaps[1]->base_address)[i]=info->colorable ? 0xFF0000FFUL : 0xFF000000UL;
        bitmap_rebuild(asset->bitmaps[1]);
    }
    else
    {
        long p,count=asset->bitmaps[1]->width*asset->bitmaps[1]->height;
        for(p=0;p<count;p++)
        {pixel32 *pixel=(pixel32 *)asset->bitmaps[1]->base_address+p;*pixel=0xFF000000UL|((*pixel>>16)&255);}
        bitmap_rebuild(asset->bitmaps[1]);
    }
    for(i=0;i<2;i++)
    {
        unsigned long bytes=asset->bitmaps[i]->width*asset->bitmaps[i]->height*12UL;
        if(bytes>0x8000000UL-geometry_bytes)return FALSE;
        geometry_bytes+=bytes;asset->memory_bytes+=bytes;
    }
    for(i=0;i<5;i++)if(!geometry_make(asset,index,i,stock))return FALSE;
    asset->model=*stock;
    asset->model.regions.count=1;asset->model.regions.address=&asset->region;
    asset->model.geometries.count=5;asset->model.geometries.address=asset->geometry;
    asset->model.shaders.count=1;asset->model.shaders.address=&asset->shader_ref;
    strcpy(asset->region.name,"cyborg");asset->region.permutations.count=1;asset->region.permutations.address=&asset->permutation;
    strcpy(asset->permutation.name,"__base");
    for(i=0;i<5;i++)asset->permutation.geometry_indices[i]=i;
    memcpy(&asset->shader,tag_get('soso',stock_shader),sizeof(asset->shader));
    asset->shader.flags=4;asset->shader.type=0;asset->shader.translucency=0;
    asset->shader.color_source=info->colorable ? 3 : 0;
    asset->shader.illumination_source=0;asset->shader.illumination_flags=0;
    memset(&asset->shader.illumination_lower,0,sizeof(asset->shader.illumination_lower));memset(&asset->shader.illumination_upper,0,sizeof(asset->shader.illumination_upper));
    asset->shader.u_scale=asset->shader.v_scale=1.f;
    asset->shader.detail_scale=asset->shader.detail_v_scale=1.f;
    asset->shader.perpendicular_brightness=asset->shader.parallel_brightness=0;
    reference_set(&asset->shader.base_map,'bitm',0x6F000000L|(first+2));
    reference_set(&asset->shader.multipurpose,'bitm',0x6F000000L|(first+3));
    reference_set(&asset->shader.detail,'bitm',NONE);reference_set(&asset->shader.cubemap,'bitm',NONE);
    memset(asset->shader.animation,0,sizeof(asset->shader.animation));
    reference_set(&asset->shader_ref.shader,'soso',0x6F000000L|(first+1));
    for(i=0;i<4;i++)snprintf(asset->names[i],sizeof(asset->names[i]),"custom_models\\%s\\%s",info->id,i==0 ? "model" : i==1 ? "armor" : i==2 ? "bitmap" : "mask");
    tag_set(first,'mode',&asset->model,asset->names[0]);tag_set(first+1,'soso',&asset->shader,asset->names[1]);
    for(i=0;i<2;i++)
    {
        asset->bitmap_groups[i].sequences.count=1;asset->bitmap_groups[i].sequences.address=&asset->sequences[i];
        asset->sequences[i].bitmap_count=1;
        asset->bitmap_groups[i].bitmaps.count=1;asset->bitmap_groups[i].bitmaps.address=asset->bitmaps[i];
        tag_set(first+2+i,'bitm',&asset->bitmap_groups[i],asset->names[2+i]);
    }
    strcpy(asset->id,info->id);asset->tag=0x6F000000L|first;asset->ready=TRUE;
    platform_log("custom models: loaded %s (five LODs, profile color %s)",info->id,info->colorable ? "enabled" : "disabled");
    return TRUE;
}
void custom_models_loaded(void)
{
    struct model *stock;short i;long total;
    halo_custom_models_scan();
    memset(objects,0,sizeof(objects));
    stock_model=tag_loaded('mode',"characters\\cyborg\\cyborg");
    if(stock_model==NONE)stock_model=tag_loaded('mode',"characters\\cyborg_mp\\cyborg_mp");
    if(stock_model==NONE)return;
    stock=model_definition_get(stock_model);if(stock->nodes.count!=19)return;
    original_table=cache_files_tag_instances(&original_count);
    total=original_count+(halo_custom_models_count()-1)*4;
    if(!original_table || total>=65535 || total==original_count){original_table=NULL;return;}
    new_table=zero_allocate(total*sizeof(*new_table));if(!new_table){original_table=NULL;return;}
    memcpy(new_table,original_table,original_count*sizeof(*new_table));
    /* Install the table before texture/shader construction asks for a tag. */
    cache_files_set_tag_instances(new_table,original_count);
    total=original_count;
    for(i=1;i<halo_custom_models_count();i++)
    {
        if(asset_make(i,stock,total)){total+=4;cache_files_set_tag_instances(new_table,total);}
        else {platform_log("custom models: %s is incomplete or has an incompatible Chief skeleton; retaining Chief",halo_custom_model_info(i)->id);asset_dispose(&assets[i]);}
    }
}
void custom_models_unloaded(void)
{
    short i;
    custom_models_preview_forget();
    for(i=1;i<HALO_CUSTOM_MODELS;i++)asset_dispose(&assets[i]);
    if(original_table)cache_files_set_tag_instances(original_table,original_count);
    if(new_table)free(new_table);
    original_table=new_table=NULL;original_count=0;stock_model=NONE;geometry_bytes=0;
    memset(objects,0,sizeof(objects));
}
static long model_for_player(long player)
{
    char const *id=custom_models_player_id(player);
    short index;
    for(index=1;index<HALO_CUSTOM_MODELS;index++)if(assets[index].ready && !strcmp(assets[index].id,id))return assets[index].tag;
    return NONE;
}
void custom_models_object_created(long index)
{
    struct object_datum *object=object_try_and_get(index);long slot;
    if(!object)return;
    slot=DATUM_INDEX_TO_ABSOLUTE_INDEX(index);
    if(slot>=NUMBEROF(objects))return;
    objects[slot].handle=index;objects[slot].tag=NONE;
    if(object->object.type==_object_type_biped && object->object.owner_player_index!=NONE)
        objects[slot].tag=model_for_player(object->object.owner_player_index);
}
void custom_models_player_changed(long index)
{
    struct player_datum *player=player_try_and_get(index);
    if(player && player->unit_index!=NONE)custom_models_object_created(player->unit_index);
}
long custom_models_render_model(long index,long original)
{
    struct object_datum *object;
    struct player_datum *player;long slot,tag;
    if(original!=stock_model || index==NONE)return original;
    object=object_try_and_get(index);
    if(!object || object->object.type!=_object_type_biped)return original;
    slot=DATUM_INDEX_TO_ABSOLUTE_INDEX(index);if(slot>=NUMBEROF(objects))return original;
    if(objects[slot].handle!=index)custom_models_object_created(index);
    player=player_try_and_get(object->object.owner_player_index);
    if(player && player->unit_index==index)
    {tag=model_for_player(object->object.owner_player_index);objects[slot].tag=tag;}
    return objects[slot].tag!=NONE ? objects[slot].tag : original;
}
