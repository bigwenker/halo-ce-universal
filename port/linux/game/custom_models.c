/* Per-object cosmetic models built as ordinary model, shader and bitmap tags.
   The original unit, animation graph, collision and replicated definition
   remain intact. Chief's validated node order drives every replacement. */
#include "cseries.h"
#include "custom_models.h"
#include "halo_custom_models.h"
#include "halo_custom_retarget.h"
#include "player_profile_rgb.h"
#include "models/model_definitions.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_model_types.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "game/players.h"
#include "game/game_globals.h"
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
typedef char custom_matrix_size[sizeof(struct halo_custom_matrix)==sizeof(real_matrix4x3) ? 1:-1];
struct custom_asset
{
    struct model model;
    struct model_node nodes[HALO_CUSTOM_MAX_NODES];
    struct halo_custom_retarget retarget;
    unsigned char skeleton_key[32];
    struct model_marker markers[256];struct model_marker_instance marker_instances[256];
    short limb_nodes[4][3];
    short clavicle_nodes[2];
    unsigned int contact_changes;
    struct halo_custom_matrix source_contact[4],target_contact_inverse[4];
    struct custom_shader shader[HALO_CUSTOM_MATERIALS];
    struct custom_shader_ref shader_ref[HALO_CUSTOM_MATERIALS];
    struct custom_geometry geometry;struct custom_part parts[HALO_CUSTOM_MATERIALS];
    struct model_region region;struct model_region_permutation permutation;
    struct bitmap_group bitmap_groups[HALO_CUSTOM_MATERIALS][2];struct bitmap_group_sequence sequences[HALO_CUSTOM_MATERIALS][2];
    struct bitmap_data *bitmaps[HALO_CUSTOM_MATERIALS][2];
    void *vertex_data[HALO_CUSTOM_MATERIALS],*index_data[HALO_CUSTOM_MATERIALS];
    char names[1+HALO_CUSTOM_MATERIALS*3][128],id[32];long tag;
    boolean first_person;
    short material_count;struct halo_custom_material materials[HALO_CUSTOM_MATERIALS];
    boolean ready;unsigned long memory_bytes;
};
static struct custom_asset assets[HALO_CUSTOM_MODELS],arms_assets[HALO_CUSTOM_MODELS];
static struct custom_tag_instance *original_table,*new_table;
static long original_count, tag_capacity, stock_model=NONE,stock_hands=NONE;
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
static struct bitmap_data *bitmap_from_image(struct halo_custom_image *image)
{
    struct bitmap_data *bitmap=bitmap_2d_new((short)image->width,(short)image->height,0,11);
    if(bitmap && bitmap->base_address)
    {memcpy(bitmap->base_address,image->pixels,image->width*image->height*4);bitmap_rebuild(bitmap);}
    halo_custom_image_dispose(image);
    return bitmap;
}
static struct bitmap_data *image_bitmap(short index,short kind)
{
    struct halo_custom_image image;
    return halo_custom_image_load(index,kind,&image) ? bitmap_from_image(&image) : NULL;
}
static struct bitmap_data *material_bitmap(short index,char const *texture,boolean mask)
{
    struct halo_custom_image image;
    return halo_custom_material_image_load(index,texture,mask,&image) ? bitmap_from_image(&image) : NULL;
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
/* Chief's animation rig remains authoritative. Bind exported vertex weights
   by bone name, independent of source order, hierarchy or rest transforms. */
static boolean skeleton_remap(struct halo_custom_mesh *mesh,struct model const *stock)
{
    short map[HALO_CUSTOM_MAX_NODES];
    short i,j,k;
    long vertex;short count=(short)stock->nodes.count;
    if(count<1 || count>HALO_CUSTOM_MAX_NODES || mesh->node_count!=count)return FALSE;
    for(i=0;i<count;i++)
    {
        for(j=0;j<count;j++)
            if(!strcmp(mesh->nodes[i].name,((struct model_node const *)stock->nodes.address)[j].name))break;
        if(j==count)return FALSE;
        for(k=0;k<i;k++)if(map[k]==j)return FALSE;
        map[i]=j;
    }
    for(vertex=0;vertex<mesh->vertex_count;vertex++)
    {
        struct halo_custom_vertex *v=&mesh->vertices[vertex];
        if(v->node[0]<0 || v->node[0]>=count || v->node[1]<-1 || v->node[1]>=count)return FALSE;
        v->node[0]=map[v->node[0]];
        if(v->node[1]>=0)v->node[1]=map[v->node[1]];
    }
    return TRUE;
}
/* Canonicalize the parsed skeleton into the animation rig's node order.
   LODs can reorder bone records but must describe the same actual skeleton. */
static boolean skeleton_nodes(struct halo_custom_mesh const *mesh,struct model const *stock,struct halo_custom_node source[HALO_CUSTOM_MAX_NODES],struct halo_custom_node target[HALO_CUSTOM_MAX_NODES])
{
    struct model_node const *nodes=stock->nodes.address;short map[HALO_CUSTOM_MAX_NODES];int i,j,count=(int)stock->nodes.count;
    memset(source,0,HALO_CUSTOM_MAX_NODES*sizeof(*source));memset(target,0,HALO_CUSTOM_MAX_NODES*sizeof(*target));
    if(count<1 || count>HALO_CUSTOM_MAX_NODES || mesh->node_count!=count)return FALSE;
    for(i=0;i<count;i++)
    {
        strcpy(source[i].name,nodes[i].name);source[i].parent=nodes[i].parent_node_index;
        memcpy(source[i].translation,&nodes[i].default_translation,12);memcpy(source[i].rotation,&nodes[i].default_rotation,16);
        for(j=0;j<count;j++)if(!strcmp(mesh->nodes[i].name,nodes[j].name))break;if(j==count)return FALSE;map[i]=(short)j;
        for(j=0;j<i;j++)if(map[j]==map[i])return FALSE;
    }
    for(i=0;i<count;i++)
    {
        int parent=mesh->nodes[i].parent;if(parent< -1 || parent>=count)return FALSE;
        target[map[i]]=mesh->nodes[i];target[map[i]].parent=parent<0 ? -1:map[parent];
    }
    return TRUE;
}
static int marker_compare(void const *a,void const *b)
{return _stricmp(((struct model_marker const *)a)->name,((struct model_marker const *)b)->name);}
static boolean asset_markers(struct custom_asset *a,struct halo_custom_mesh const *mesh,struct model const *stock)
{
    int i,j,count=0;struct halo_custom_node n;struct halo_custom_matrix matrix;
    /* One cosmetic permutation. Stock markers absent from the JMS retain
       their bone-local offsets; explicit JMS markers override those names. */
    for(i=0;i<stock->markers.count;i++)
    {
        struct model_marker const *marker=(struct model_marker const *)stock->markers.address+i;
        struct model_marker_instance const *instance=marker->instances.address;
        for(j=0;j<mesh->marker_count;j++)if(!_stricmp(marker->name,mesh->markers[j].name))break;
        if(j<mesh->marker_count || !marker->instances.count)continue;
        if(count==256 || instance->node_index>=a->retarget.count)return FALSE;
        strcpy(a->markers[count].name,marker->name);a->marker_instances[count]=*instance;
        a->marker_instances[count].region_index=0;a->marker_instances[count].permutation_index=0;count++;
    }
    for(i=0;i<mesh->marker_count;i++)
    {
        struct halo_custom_marker const *m=&mesh->markers[i];struct model_marker_instance *instance;
        /* This mesh has a single permutation: ambiguous duplicate names
           cannot select a meaningful attachment, so reject rather than guess. */
        for(j=0;j<i;j++)if(!_stricmp(m->name,mesh->markers[j].name))return FALSE;
        if(count==256)return FALSE;strcpy(a->markers[count].name,m->name);instance=&a->marker_instances[count];
        instance->node_index=(byte)(m->node<0 ? 0:a->retarget.map[m->node]);
        memcpy(&instance->translation,m->translation,12);memcpy(&instance->rotation,m->rotation,16);
        if(m->node<0)
        {
            memset(&n,0,sizeof(n));memcpy(n.translation,m->translation,12);memcpy(n.rotation,m->rotation,16);
            halo_custom_matrix_node(&n,&matrix);halo_custom_matrix_multiply(&a->retarget.target_inverse[0],&matrix,&matrix);
            memcpy(&instance->translation,matrix.v[3],12);matrix4x3_rotation_to_quaternion((real_matrix4x3 *)&matrix,&instance->rotation);
        }
        count++;
    }
    for(i=0;i<count;i++){a->markers[i].instances.count=1;a->markers[i].instances.address=&a->marker_instances[i];}
    qsort(a->markers,count,sizeof(a->markers[0]),marker_compare);a->model.markers.address=a->markers;a->model.markers.count=count;
    return TRUE;
}
static boolean contact_matrix(struct model const *model,char const *name,struct halo_custom_matrix *matrix,int *node)
{
    int i;struct halo_custom_node n;
    for(i=0;i<model->markers.count;i++)
    {
        struct model_marker const *m=(struct model_marker const *)model->markers.address+i;
        if(!_stricmp(m->name,name) && m->instances.count)
        {
            struct model_marker_instance const *p=m->instances.address;
            if(p->node_index>=model->nodes.count)return FALSE;*node=p->node_index;
            memset(&n,0,sizeof(n));memcpy(n.rotation,&p->rotation,16);memcpy(n.translation,&p->translation,12);halo_custom_matrix_node(&n,matrix);return TRUE;
        }
    }
    return FALSE;
}
static boolean asset_skeleton(struct custom_asset *a,short index,struct halo_custom_mesh const *mesh,struct model const *stock)
{
    struct halo_custom_node source[HALO_CUSTOM_MAX_NODES],target[HALO_CUSTOM_MAX_NODES];int i,j,limb,bone,node,hand,count=(int)stock->nodes.count;struct halo_custom_matrix contact,bind;boolean cached;
    static char const *limbs[4][3]={{"bip01 l upperarm","bip01 l forearm","bip01 l hand"},{"bip01 r upperarm","bip01 r forearm","bip01 r hand"},{"bip01 l thigh","bip01 l calf","bip01 l foot"},{"bip01 r thigh","bip01 r calf","bip01 r foot"}};
    static char const *markers[4]={"left hand","right hand","left foot","right foot"};
    if(!skeleton_nodes(mesh,stock,source,target) || !halo_custom_skeleton_hash_count(count,source,target,a->skeleton_key))return FALSE;
    cached=halo_custom_retarget_cache_load(index,a->first_person,a->skeleton_key,&a->retarget) && a->retarget.count==count;
    if(cached)for(i=0;i<count;i++)if(a->retarget.parent[i]!=source[i].parent || a->retarget.map[i]!=i)cached=FALSE;
    if(cached)platform_log("custom models: reused %s retarget cache for %s",a->first_person ? "first-person" : "body",halo_custom_model_info(index)->id);
    else
    {
        if(!halo_custom_retarget_build_count(count,source,target,&a->retarget))return FALSE;
        halo_custom_retarget_cache_save(index,a->first_person,a->skeleton_key,&a->retarget);platform_log("custom models: generated %s retarget cache for %s",a->first_person ? "first-person" : "body",halo_custom_model_info(index)->id);
    }
    /* The canonical input order makes plan.map identity. Geometry remapping
       uses the original JMS order separately. */
    for(i=0;i<count;i++)
    {
        a->nodes[i]=((struct model_node *)stock->nodes.address)[i];
        memcpy(&a->nodes[i].default_translation,target[i].translation,12);memcpy(&a->nodes[i].default_rotation,target[i].rotation,16);
        memcpy(&a->nodes[i].runtime_default_inverse_matrix,&a->retarget.target_inverse[i],sizeof(real_matrix4x3));
    }
    a->model=*stock;a->model.nodes.address=a->nodes;
    /* Marker node indices come from the JMS's own order. */
    for(i=0;i<count;i++)for(j=0;j<count;j++)if(!strcmp(mesh->nodes[i].name,source[j].name)){a->retarget.map[i]=j;break;}
    if(!asset_markers(a,mesh,stock))return FALSE;
    if(a->first_person)
    {
        static char const *arms[2][3]={{"frame l upperarm","frame l forearm","frame l wriste"},{"frame r upperarm","frame r forearm","frame r wriste"}};
        for(limb=0;limb<2;limb++)for(bone=0;bone<3;bone++)
        {for(i=0;i<count;i++)if(!strcmp(source[i].name,arms[limb][bone]))break;a->limb_nodes[limb][bone]=i<count ? (short)i:NONE;}
    }
    for(limb=0;limb<4;limb++)
    {
        if(a->first_person && limb>=2)break;
        if(!a->first_person)for(bone=0;bone<3;bone++)
        {for(i=0;i<count;i++)if(!strcmp(source[i].name,limbs[limb][bone]))break;if(i==count)return FALSE;a->limb_nodes[limb][bone]=(short)i;}
        if(limb<2)
        {
            a->clavicle_nodes[limb]=NONE;i=a->limb_nodes[limb][0];
            if(i>=0 && source[i].parent>=0 && strstr(source[source[i].parent].name,"clavicle"))a->clavicle_nodes[limb]=(short)source[i].parent;
        }
        hand=a->limb_nodes[limb][2];
        if(hand==NONE)continue;
        memset(&a->source_contact[limb],0,sizeof(a->source_contact[limb]));a->source_contact[limb].scale=1;
        for(i=0;i<3;i++)a->source_contact[limb].v[i][i]=1;
        if(contact_matrix(stock,markers[limb],&contact,&node))
        {
            halo_custom_matrix_inverse(&a->retarget.source_inverse[node],&bind);
            halo_custom_matrix_multiply(&bind,&contact,&contact);
            halo_custom_matrix_multiply(&a->retarget.source_inverse[hand],&contact,&a->source_contact[limb]);
        }
        else if(!a->first_person)return FALSE;
        contact=a->source_contact[limb];
        if(contact_matrix(&a->model,markers[limb],&contact,&node))
        {
            halo_custom_matrix_multiply(&a->retarget.target[node],&contact,&contact);
            halo_custom_matrix_multiply(&a->retarget.target_inverse[hand],&contact,&contact);
        }
        else contact=a->source_contact[limb];
        for(i=0;i<12;i++)if(fabsf(((float *)a->source_contact[limb].v)[i]-((float *)contact.v)[i])>1.e-5f)a->contact_changes|=1u<<limb;
        halo_custom_matrix_inverse(&contact,&a->target_contact_inverse[limb]);
    }
    return TRUE;
}
static boolean geometry_part_make(struct custom_asset *asset,short slot,short shader,struct halo_custom_mesh const *mesh)
{
    struct model_vertex_uncompressed *vertices;
    struct model_vertex_compressed *compressed;
    struct custom_part *part=&asset->parts[slot];
    unsigned short *indices;
    long i;unsigned long bytes=3*(mesh->vertex_count*sizeof(*compressed)+mesh->triangle_count*6);
    if(bytes>0x8000000UL-geometry_bytes)return FALSE;
    vertices=zero_allocate(mesh->vertex_count*sizeof(*vertices));compressed=malloc(mesh->vertex_count*sizeof(*compressed));
    if(!vertices || !compressed){if(vertices)free(vertices);if(compressed)free(compressed);return FALSE;}
    for(i=0;i<mesh->vertex_count;i++)
    {
        struct halo_custom_vertex *v=&mesh->vertices[i];
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
    rasterizer_geometry_compress_vertices(_rasterizer_vertex_type_model_uncompressed,mesh->vertex_count,compressed,
        mesh->vertex_count*sizeof(*compressed),vertices,mesh->vertex_count*sizeof(*vertices));
    part->shader=shader;part->previous=part->next=NONE;part->centroid_weight[0]=1.f;
    asset->vertex_data[slot]=compressed;
    indices=malloc(mesh->triangle_count*6);asset->index_data[slot]=indices;
    if(!indices){free(vertices);return FALSE;}
    for(i=0;i<mesh->triangle_count*3;i++)indices[i]=(unsigned short)mesh->triangles[i];
    if(!rasterizer_vertex_buffer_new(&part->vertex_buffer,_rasterizer_vertex_type_model_compressed,mesh->vertex_count,compressed,mesh->vertex_count*sizeof(*compressed)) ||
        !rasterizer_triangle_buffer_new(&part->triangle_buffer,_triangle_buffer_type_triangles,mesh->triangle_count,indices))
    {free(vertices);return FALSE;}
    geometry_bytes+=bytes;asset->memory_bytes+=bytes;
    free(vertices);return TRUE;
}
static boolean geometry_make(struct custom_asset *asset,struct halo_custom_mesh const *mesh)
{
    struct halo_custom_mesh part;
    short material,slot;
    asset->geometry.parts.address=asset->parts;
    for(material=0;material<mesh->material_count;material++)
    {
        if(!halo_custom_mesh_material(mesh,material,&part))return FALSE;
        if(!part.triangle_count){halo_custom_mesh_dispose(&part);continue;}
        slot=(short)asset->geometry.parts.count++;
        if(!geometry_part_make(asset,slot,material,&part))
        {halo_custom_mesh_dispose(&part);return FALSE;}
        halo_custom_mesh_dispose(&part);
    }
    return TRUE;
}
static void asset_dispose(struct custom_asset *asset)
{
    short i,j;
    for(j=0;j<HALO_CUSTOM_MATERIALS;j++)
    {
        struct custom_part *part=&asset->parts[j];
        if(part->vertex_buffer.hardware_format)rasterizer_vertex_buffer_delete(&part->vertex_buffer);
        if(part->triangle_buffer.hardware_format)rasterizer_triangle_buffer_delete(&part->triangle_buffer);
        if(asset->vertex_data[j])free(asset->vertex_data[j]);if(asset->index_data[j])free(asset->index_data[j]);
    }
    for(i=0;i<HALO_CUSTOM_MATERIALS;i++)for(j=0;j<2;j++)if(asset->bitmaps[i][j])bitmap_delete(asset->bitmaps[i][j]);
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
static boolean material_make(struct custom_asset *asset,short index,short material,long stock_shader,long first)
{
    struct halo_custom_model_info const *info=halo_custom_model_info(index);
    struct custom_shader *shader=&asset->shader[material];
    long i,base=first+1+material*3;
    char const *texture=asset->materials[material].texture;
    asset->bitmaps[material][0]=material_bitmap(index,texture,FALSE);
    if(!asset->bitmaps[material][0])return FALSE;
    if(info->colorable)asset->bitmaps[material][1]=material_bitmap(index,texture,TRUE);
    if(!asset->bitmaps[material][1])
    {
        asset->bitmaps[material][1]=bitmap_2d_new(4,4,0,11);
        if(!asset->bitmaps[material][1])return FALSE;
        for(i=0;i<16;i++)((pixel32 *)asset->bitmaps[material][1]->base_address)[i]=info->colorable ? 0xFF0000FFUL : 0xFF000000UL;
        bitmap_rebuild(asset->bitmaps[material][1]);
    }
    else
    {
        long p,count=asset->bitmaps[material][1]->width*asset->bitmaps[material][1]->height;
        for(p=0;p<count;p++)
        {pixel32 *pixel=(pixel32 *)asset->bitmaps[material][1]->base_address+p;*pixel=0xFF000000UL|((*pixel>>16)&255);}
        bitmap_rebuild(asset->bitmaps[material][1]);
    }
    for(i=0;i<2;i++)
    {
        unsigned long bytes=asset->bitmaps[material][i]->width*asset->bitmaps[material][i]->height*12UL;
        if(bytes>0x8000000UL-geometry_bytes)return FALSE;
        geometry_bytes+=bytes;asset->memory_bytes+=bytes;
    }
    memcpy(shader,tag_get('soso',stock_shader),sizeof(*shader));
    shader->flags=4;shader->type=0;shader->translucency=0;
    shader->color_source=info->colorable ? 3 : 0;
    shader->illumination_source=0;shader->illumination_flags=0;
    memset(&shader->illumination_lower,0,sizeof(shader->illumination_lower));memset(&shader->illumination_upper,0,sizeof(shader->illumination_upper));
    shader->u_scale=shader->v_scale=1.f;shader->detail_scale=shader->detail_v_scale=1.f;
    shader->perpendicular_brightness=shader->parallel_brightness=0;
    reference_set(&shader->base_map,'bitm',0x6F000000L|(base+1));
    reference_set(&shader->multipurpose,'bitm',0x6F000000L|(base+2));
    reference_set(&shader->detail,'bitm',NONE);reference_set(&shader->cubemap,'bitm',NONE);
    memset(shader->animation,0,sizeof(shader->animation));
    reference_set(&asset->shader_ref[material].shader,'soso',0x6F000000L|base);
    snprintf(asset->names[1+material*3],128,"custom_models\\%s\\%s%s",info->id,asset->first_person ? "fp\\" : "",asset->materials[material].name);
    tag_set(base,'soso',shader,asset->names[1+material*3]);
    for(i=0;i<2;i++)
    {
        asset->bitmap_groups[material][i].sequences.count=1;asset->bitmap_groups[material][i].sequences.address=&asset->sequences[material][i];
        asset->sequences[material][i].bitmap_count=1;
        asset->bitmap_groups[material][i].bitmaps.count=1;asset->bitmap_groups[material][i].bitmaps.address=asset->bitmaps[material][i];
        snprintf(asset->names[2+material*3+i],128,"custom_models\\%s\\%s%s_%s",info->id,asset->first_person ? "fp\\" : "",texture,i ? "mask" : "bitmap");
        tag_set(base+1+i,'bitm',&asset->bitmap_groups[material][i],asset->names[2+material*3+i]);
    }
    return TRUE;
}
static boolean asset_make(struct custom_asset *asset,short index,boolean first_person,struct model *stock,long first)
{
    short i;long stock_shader;
    struct halo_custom_mesh mesh;
    struct halo_custom_model_info const *info=halo_custom_model_info(index);
    stock_shader=tag_loaded('soso',"characters\\cyborg\\shaders\\armor");
    if(stock_shader==NONE)stock_shader=tag_loaded('soso',"characters\\cyborg_mp\\shaders\\armor");
    if(stock_shader==NONE || !halo_custom_mesh_load(index,first_person,&mesh))return FALSE;
    if(first+1+mesh.material_count*3>tag_capacity){halo_custom_mesh_dispose(&mesh);return FALSE;}
    asset->first_person=first_person;
    if(!asset_skeleton(asset,index,&mesh,stock)){halo_custom_mesh_dispose(&mesh);return FALSE;}
    if(!skeleton_remap(&mesh,stock)){halo_custom_mesh_dispose(&mesh);return FALSE;}
    asset->material_count=(short)mesh.material_count;
    memcpy(asset->materials,mesh.materials,mesh.material_count*sizeof(*mesh.materials));
    for(i=0;i<asset->material_count;i++)if(!material_make(asset,index,i,stock_shader,first))
    {halo_custom_mesh_dispose(&mesh);return FALSE;}
    if(!geometry_make(asset,&mesh)){halo_custom_mesh_dispose(&mesh);return FALSE;}
    halo_custom_mesh_dispose(&mesh);
    asset->model.regions.count=1;asset->model.regions.address=&asset->region;
    asset->model.geometries.count=1;asset->model.geometries.address=&asset->geometry;
    asset->model.shaders.count=asset->material_count;asset->model.shaders.address=asset->shader_ref;
    strcpy(asset->region.name,"cyborg");asset->region.permutations.count=1;asset->region.permutations.address=&asset->permutation;
    strcpy(asset->permutation.name,"__base");for(i=0;i<5;i++)asset->permutation.geometry_indices[i]=0;
    snprintf(asset->names[0],128,"custom_models\\%s\\%smodel",info->id,first_person ? "fp\\" : "");tag_set(first,'mode',&asset->model,asset->names[0]);
    strcpy(asset->id,info->id);asset->tag=0x6F000000L|first;asset->ready=TRUE;
    platform_log("custom models: loaded %s (single JMS, %d materials, profile color %s)",info->id,asset->material_count,info->colorable ? "enabled" : "disabled");
    return TRUE;
}
void custom_models_loaded(void)
{
    struct model *stock;struct game_globals *globals;short i,kind;long total,globals_tag;
    halo_custom_models_scan();memset(objects,0,sizeof(objects));
    stock_model=tag_loaded('mode',"characters\\cyborg\\cyborg");
    if(stock_model==NONE)stock_model=tag_loaded('mode',"characters\\cyborg_mp\\cyborg_mp");
    stock_hands=NONE;globals_tag=tag_loaded('matg',"globals\\globals");
    if(globals_tag!=NONE)
    {
        globals=tag_get('matg',globals_tag);
        if(globals->first_person_interface.count>0)
            stock_hands=((struct game_globals_first_person_interface *)globals->first_person_interface.address)->hands.index;
    }
    if(stock_model==NONE && stock_hands==NONE)return;
    original_table=cache_files_tag_instances(&original_count);
    tag_capacity=MIN(65535L,original_count+(halo_custom_models_count()-1)*2*(1+HALO_CUSTOM_MATERIALS*3));
    if(!original_table || tag_capacity<=original_count){original_table=NULL;return;}
    new_table=zero_allocate(tag_capacity*sizeof(*new_table));if(!new_table){original_table=NULL;return;}
    memcpy(new_table,original_table,original_count*sizeof(*new_table));cache_files_set_tag_instances(new_table,original_count);
    total=original_count;
    for(i=1;i<halo_custom_models_count();i++)for(kind=0;kind<2;kind++)
    {
        struct custom_asset *asset=kind ? &arms_assets[i]:&assets[i];long model=kind ? stock_hands:stock_model;
        if(model==NONE)continue;stock=model_definition_get(model);
        if(!kind && stock->nodes.count!=HALO_CUSTOM_MODEL_NODES)continue;
        if(asset_make(asset,i,kind!=0,stock,total))
        {total+=1+asset->material_count*3;cache_files_set_tag_instances(new_table,total);}
        else
        {platform_log("custom models: %s has no valid %s model; retaining Chief",halo_custom_model_info(i)->id,kind ? "first-person" : "body");asset_dispose(asset);}
    }
}
void custom_models_unloaded(void)
{
    short i;
    custom_models_preview_forget();
    for(i=1;i<HALO_CUSTOM_MODELS;i++){asset_dispose(&assets[i]);asset_dispose(&arms_assets[i]);}
    if(original_table)cache_files_set_tag_instances(original_table,original_count);
    if(new_table)free(new_table);
    original_table=new_table=NULL;original_count=0;tag_capacity=0;stock_model=stock_hands=NONE;geometry_bytes=0;
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
boolean custom_models_render_nodes(long index,long model,real_matrix4x3 const *source,real_matrix4x3 *target)
{
    struct custom_asset *a=NULL;struct halo_custom_matrix input[19],output[19],goal;struct object_datum *object;
    real_matrix4x3 const *raw=NULL;boolean legs=FALSE;
    unsigned int constraints;int i,j,limb;
    for(i=1;i<HALO_CUSTOM_MODELS;i++)if(assets[i].ready && assets[i].tag==model){a=&assets[i];break;}
    if(!a)return FALSE;
    if(a->retarget.identical && !a->contact_changes){memcpy(target,source,19*sizeof(*target));return TRUE;}
    object=object_try_and_get(index);
    if(object)
    {
        raw=object_get_node_matrices(index);
        if(raw && raw!=source)for(i=1;i<19;i++)if(source[i].scale==.001f && raw[i].scale!=.001f){legs=TRUE;break;}
    }
    /* First-person legs deliberately collapse the torso. Retarget the
       original pose, then reapply that cosmetic fold and setback. */
    memcpy(input,legs ? raw:source,sizeof(input));if(!halo_custom_retarget_pose(&a->retarget,input,output))return FALSE;
    constraints=object && !legs ? unit_custom_model_ik(index):0;
    if(a->retarget.identical)constraints&=a->contact_changes;
    for(limb=0;limb<4;limb++)if(constraints&(1u<<limb))
    {
        short const *nodes=a->limb_nodes[limb];
        halo_custom_matrix_multiply(&input[nodes[2]],&a->source_contact[limb],&goal);
        halo_custom_matrix_multiply(&goal,&a->target_contact_inverse[limb],&goal);
        halo_custom_retarget_contact(&a->retarget,input,output,limb<2 ? a->clavicle_nodes[limb]:NONE,nodes[0],nodes[1],nodes[2],&goal);
    }
    if(legs)for(i=0;i<19;i++)
    {
        if(source[i].scale==.001f){memcpy(output[i].v[3],&source[i].position,12);output[i].scale=.001f;}
        else for(j=0;j<3;j++)output[i].v[3][j]+=source[0].n[3][j]-raw[0].n[3][j];
    }
    memcpy(target,output,sizeof(output));return TRUE;
}
long custom_models_first_person_model(long unit,long original)
{
    struct object_datum *object=object_try_and_get(unit);struct player_datum *player;char const *id;short i;
    if(original!=stock_hands || !object || object->object.type!=_object_type_biped)return original;
    player=player_try_and_get(object->object.owner_player_index);
    if(!player || player->unit_index!=unit)return original;
    id=custom_models_player_id(object->object.owner_player_index);
    for(i=1;i<HALO_CUSTOM_MODELS;i++)if(arms_assets[i].ready && !strcmp(arms_assets[i].id,id))return arms_assets[i].tag;
    return original;
}
boolean custom_models_first_person_nodes(long model,real_matrix4x3 *nodes)
{
    struct custom_asset *asset=NULL;struct halo_custom_matrix input[HALO_CUSTOM_MAX_NODES],output[HALO_CUSTOM_MAX_NODES];
    struct halo_custom_matrix goal;short i,limb;long bytes;
    for(i=1;i<HALO_CUSTOM_MODELS;i++)if(arms_assets[i].ready && arms_assets[i].tag==model){asset=&arms_assets[i];break;}
    if(!asset)return FALSE;
    bytes=asset->retarget.count*sizeof(real_matrix4x3);memcpy(input,nodes,bytes);
    if(!halo_custom_retarget_pose(&asset->retarget,input,output))return FALSE;
    if(!asset->retarget.identical || asset->contact_changes)for(limb=0;limb<2;limb++)
    {
        short const *nodes=asset->limb_nodes[limb];if(nodes[2]==NONE)continue;
        halo_custom_matrix_multiply(&input[nodes[2]],&asset->source_contact[limb],&goal);
        halo_custom_matrix_multiply(&goal,&asset->target_contact_inverse[limb],&goal);
        halo_custom_retarget_contact(&asset->retarget,input,output,asset->clavicle_nodes[limb],nodes[0],nodes[1],nodes[2],&goal);
    }
    memcpy(nodes,output,bytes);return TRUE;
}
