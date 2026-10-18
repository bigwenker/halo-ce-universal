/* User-authored assets beside the executable; game tags are built by
   game/custom_models.c. Missing or malformed assets always retain Chief. */
#include "halo_custom_models.h"
#include "halo_custom_retarget.h"
#include "platform.h"
#include "tomlc17.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../third_party/stb/stb_image.h"

static struct halo_custom_model_info catalogue[HALO_CUSTOM_MODELS];
static int model_count;
static char root[4096];

static int retarget_path(int index,int first_person,char path[4096],int directory)
{
    if(index<=0 || index>=halo_custom_models_count())return 0;
    return SDL_snprintf(path,4096,"%s/%s/generated%s",root,catalogue[index].id,
        directory ? "" : first_person ? "/retarget-fp-v3.cache" : "/retarget-body-v3.cache")<4096;
}
int halo_custom_retarget_cache_load(int index,int first_person,unsigned char const key[32],struct halo_custom_retarget *plan)
{
    char path[4096];unsigned char bytes[HALO_RETARGET_CACHE_BYTES];size_t size;FILE *f;int ok;
    if(!retarget_path(index,first_person,path,0) || !(f=fopen(path,"rb")))return 0;
    size=fread(bytes,1,sizeof(bytes),f);ok=!ferror(f) && feof(f);fclose(f);
    return ok && halo_custom_retarget_decode(bytes,(unsigned int)size,key,plan);
}
void halo_custom_retarget_cache_save(int index,int first_person,unsigned char const key[32],struct halo_custom_retarget const *plan)
{
    char path[4096],temporary[4096],directory[4096];unsigned char bytes[HALO_RETARGET_CACHE_BYTES];
    FILE *f;int size,ok;
    if(!retarget_path(index,first_person,path,0) || !retarget_path(index,first_person,directory,1) || !SDL_CreateDirectory(directory) ||
        SDL_snprintf(temporary,sizeof(temporary),"%s.%llu.%p.tmp",path,(unsigned long long)SDL_GetTicks(),(void *)bytes)>=(int)sizeof(temporary))return;
    size=halo_custom_retarget_encode(plan,key,bytes);if(!size)return;f=fopen(temporary,"wb");if(!f)return;
    ok=fwrite(bytes,1,size,f)==(size_t)size;if(fclose(f))ok=0;
    /* Android's guest bridges SDL_GetTicks and ordinary file syscalls;
       SDL's newer rename/remove helpers are not guest imports. */
#ifdef HALO_WINDOWS
    if(!ok || !SDL_RenamePath(temporary,path))remove(temporary);
#else
    if(!ok || rename(temporary,path))remove(temporary);
#endif
}

int halo_custom_model_id_valid(char const *id)
{
    size_t i, n = id ? strlen(id) : 0;
    if (n >= HALO_CUSTOM_MODEL_ID) return 0;
    for (i = 0; i < n; i++)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '_' || id[i] == '-')) return 0;
    return 1;
}
static void *read_bounded(char const *path, unsigned int limit, unsigned int *size)
{
    FILE *file = fopen(path, "rb");
    char *data = NULL;
    long length;
    *size = 0;
    if (!file) return NULL;
    if (!fseek(file, 0, SEEK_END) && (length = ftell(file)) > 0 && (unsigned long)length <= limit && !fseek(file, 0, SEEK_SET))
    {
        data = malloc((size_t)length + 1);
        if (data && fread(data, 1, length, file) == (size_t)length && fgetc(file) == EOF)
        { data[length] = 0; *size = (unsigned int)length; }
        else { free(data); data = NULL; }
    }
    fclose(file);
    return data;
}
static SDL_EnumerationResult SDLCALL discover(void *unused, char const *directory, char const *name)
{
    char path[4096]; unsigned int size;
    char *data;
    SDL_PathInfo stat;
    toml_result_t result;
    toml_datum_t full, color, default_rgb;
    struct halo_custom_model_info *info;
    (void)unused;
    if (!*name || !strcmp(name,"master_chief") || !halo_custom_model_id_valid(name) || model_count == HALO_CUSTOM_MODELS) return SDL_ENUM_CONTINUE;
    if (SDL_snprintf(path,sizeof(path),"%s%s",directory,name)>=(int)sizeof(path) || !SDL_GetPathInfo(path,&stat) || stat.type!=SDL_PATHTYPE_DIRECTORY) return SDL_ENUM_CONTINUE;
    if (SDL_snprintf(path,sizeof(path),"%s/%s/info.toml",root,name)>=(int)sizeof(path) || !(data=read_bounded(path,16384,&size))) return SDL_ENUM_CONTINUE;
    result=toml_parse(data,(int)size);
    free(data);
    if (!result.ok) { platform_log("custom models: %s: %s",name,result.errmsg); toml_free(result); return SDL_ENUM_CONTINUE; }
    full=toml_seek(result.toptab,"full_name"); color=toml_seek(result.toptab,"colorable");
    if (full.type==TOML_STRING && *full.u.s && strlen(full.u.s)<sizeof(catalogue[0].name) &&
        color.type==TOML_BOOLEAN)
    {
        info=&catalogue[model_count++];
        SDL_snprintf(info->id,sizeof(info->id),"%s",name);
        SDL_snprintf(info->name,sizeof(info->name),"%s",full.u.s);
        info->colorable=color.u.boolean;
        if (info->colorable)
        {
            default_rgb=toml_seek(result.toptab,"default_rgb");
            if (default_rgb.type==TOML_STRING && strlen(default_rgb.u.s)==7)
                memcpy(info->default_rgb,default_rgb.u.s,sizeof(info->default_rgb));
        }
    }
    else platform_log("custom models: %s: expected full_name and colorable",name);
    toml_free(result);
    return SDL_ENUM_CONTINUE;
}
static int compare_models(void const *a, void const *b) { return strcmp(((struct halo_custom_model_info const *)a)->id,((struct halo_custom_model_info const *)b)->id); }
void halo_custom_models_scan(void)
{
    char const *base=SDL_GetBasePath();
    memset(catalogue,0,sizeof(catalogue)); model_count=1;
    strcpy(catalogue[0].id,"master_chief"); strcpy(catalogue[0].name,"Master Chief"); catalogue[0].colorable=1;
    if (!base || SDL_snprintf(root,sizeof(root),"%scustom_models",base)>=(int)sizeof(root) || !SDL_CreateDirectory(root)) return;
    SDL_EnumerateDirectory(root,discover,NULL);
    qsort(catalogue+1,model_count-1,sizeof(catalogue[0]),compare_models);
}
int halo_custom_models_count(void) { if (!model_count) halo_custom_models_scan(); return model_count; }
struct halo_custom_model_info const *halo_custom_model_info(int i) { return i>=0 && i<halo_custom_models_count() ? &catalogue[i] : NULL; }
int halo_custom_model_find(char const *id)
{
    int i;
    for(i=0;i<halo_custom_models_count();i++) if (!strcmp(id,catalogue[i].id)) return i;
    return 0;
}
int halo_custom_mesh_load(int index,int first_person,struct halo_custom_mesh *mesh)
{
    char path[4096];char *data;unsigned int size;int ok;
    memset(mesh,0,sizeof(*mesh));
    if(index<=0 || index>=halo_custom_models_count() || first_person<0 || first_person>1)return 0;
    if(SDL_snprintf(path,sizeof(path),"%s/%s/source/characters/cyborg/%smodels/%s.jms",root,catalogue[index].id,
        first_person ? "fp/" : "",catalogue[index].id)>=(int)sizeof(path))return 0;
    data=read_bounded(path,16u*1024u*1024u,&size);
    if(!data)return 0;
    ok=halo_custom_mesh_parse(data,size,mesh);free(data);return ok;
}
/* stb decodes palettes and pixels; reject truncated BMP rows before it can
   fill missing input with zeroes. Heights may encode top-down rows. */
static unsigned int custom_image_u32(unsigned char const *p)
{ return (unsigned int)p[0] | ((unsigned int)p[1]<<8) | ((unsigned int)p[2]<<16) | ((unsigned int)p[3]<<24); }
static int custom_bmp_dimensions(void const *data,unsigned int size,int *width,int *height)
{
    unsigned char const *p=data;
    unsigned int header,offset,w,h,bits,compression,colors,end,row;
    if(size<54 || memcmp(p,"BM",2))return 0;
    header=custom_image_u32(p+14);offset=custom_image_u32(p+10);
    if((header!=40 && header!=52 && header!=56 && header!=108 && header!=124) || header>size-14)return 0;
    w=custom_image_u32(p+18);h=custom_image_u32(p+22);if(h&0x80000000u)h=0u-h;
    bits=custom_image_u32(p+28)&65535u;compression=custom_image_u32(p+30);
    if(w<1 || w>2048 || h<1 || h>2048 || p[26]!=1 || p[27] ||
        (bits!=8 && bits!=24 && bits!=32) || (compression!=0 && !(compression==3 && bits==32)))return 0;
    end=14+header;
    if(compression==3 && header==40)end+=12;
    if(bits==8){colors=custom_image_u32(p+46);if(!colors)colors=256;if(colors>256)return 0;end+=colors*4;}
    row=((w*bits+31)/32)*4;
    if(offset<end || offset>size || h>(size-offset)/row)return 0;
    *width=(int)w;*height=(int)h;return 1;
}
void halo_custom_image_dispose(struct halo_custom_image *image) { free(image->pixels); memset(image,0,sizeof(*image)); }
static int custom_image_load_path(char *path,int kind,struct halo_custom_image *image)
{
    unsigned int size;void *data;
    unsigned char *pixels;
    int width,height,channels,w,h,x,y;
    memset(image,0,sizeof(*image));
    data=read_bounded(path,16u*1024u*1024u,&size);
    /* PNG takes precedence; BMP is the fallback for a missing texture. */
    if (!data && kind!=0)
    {
        memcpy(path+strlen(path)-3,"bmp",4);
        data=read_bounded(path,16u*1024u*1024u,&size);
    }
    if (!data) return 0;
    if (size>=2 && !memcmp(data,"BM",2))
    { if(!custom_bmp_dimensions(data,size,&width,&height)){free(data);return 0;} }
    else if(size<8 || memcmp(data,"\x89PNG\r\n\x1a\n",8) || !stbi_info_from_memory(data,size,&width,&height,&channels))
    {free(data);return 0;}
    if(width<1 || height<1 || width>2048 || height>2048 || (kind==0 && height!=width*2)){free(data);return 0;}
    pixels=stbi_load_from_memory(data,size,&width,&height,&channels,4); free(data);
    if (!pixels) return 0;
    w=kind==0 ? 128 : 1; h=kind==0 ? 256 : 1;
    while(w<width && kind!=0) w*=2;
    while(h<height && kind!=0) h*=2;
    image->pixels=malloc((size_t)w*h*4);
    if (image->pixels) for(y=0;y<h;y++) for(x=0;x<w;x++)
    {
        unsigned char *p=pixels+((y*height/h)*width+x*width/w)*4;
        image->pixels[y*w+x]=((unsigned int)p[3]<<24)|((unsigned int)p[0]<<16)|((unsigned int)p[1]<<8)|p[2];
    }
    stbi_image_free(pixels);
    if (!image->pixels) return 0;
    image->width=w;image->height=h; return 1;
}

int halo_custom_image_load(int index,int kind,struct halo_custom_image *image)
{
    char path[4096];
    memset(image,0,sizeof(*image));
    if(index<=0 || index>=halo_custom_models_count() || kind<0 || kind>2)return 0;
    if(SDL_snprintf(path,sizeof(path),"%s/%s/%s",root,catalogue[index].id,
        kind==0 ? "preview.png" : kind==1 ? "textures/armor.png" : "textures/armor_mask.png")>=(int)sizeof(path))return 0;
    return custom_image_load_path(path,kind,image);
}
int halo_custom_material_image_load(int index,char const *texture,int mask,struct halo_custom_image *image)
{
    char path[4096];
    memset(image,0,sizeof(*image));
    if(index<=0 || index>=halo_custom_models_count() || !halo_custom_texture_name_valid(texture))return 0;
    if(SDL_snprintf(path,sizeof(path),"%s/%s/textures/%s%s.png",root,catalogue[index].id,texture,mask ? "_mask" : "")>=(int)sizeof(path))return 0;
    return custom_image_load_path(path,1,image);
}
