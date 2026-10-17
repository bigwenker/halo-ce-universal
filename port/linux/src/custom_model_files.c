/* User-authored assets beside the executable; game tags are built by
   game/custom_models.c. Missing or malformed assets always retain Chief. */
#include "halo_custom_models.h"
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
static int leaf_png(char const *name)
{
    size_t n = strlen(name), i;
    if (n < 5 || n >= sizeof(catalogue[0].preview) || strcmp(name+n-4, ".png")) return 0;
    for (i=0;i<n;i++) if (name[i]=='/' || name[i]=='\\' || name[i]==':' || (unsigned char)name[i]<32) return 0;
    return strcmp(name,".") && strcmp(name,"..");
}
static SDL_EnumerationResult SDLCALL discover(void *unused, char const *directory, char const *name)
{
    char path[4096]; unsigned int size;
    char *data;
    SDL_PathInfo stat;
    toml_result_t result;
    toml_datum_t full, color, preview, default_rgb;
    struct halo_custom_model_info *info;
    (void)unused;
    if (!*name || !strcmp(name,"master_chief") || !halo_custom_model_id_valid(name) || model_count == HALO_CUSTOM_MODELS) return SDL_ENUM_CONTINUE;
    if (SDL_snprintf(path,sizeof(path),"%s%s",directory,name)>=(int)sizeof(path) || !SDL_GetPathInfo(path,&stat) || stat.type!=SDL_PATHTYPE_DIRECTORY) return SDL_ENUM_CONTINUE;
    if (SDL_snprintf(path,sizeof(path),"%s/%s/info.toml",root,name)>=(int)sizeof(path) || !(data=read_bounded(path,16384,&size))) return SDL_ENUM_CONTINUE;
    result=toml_parse(data,(int)size);
    free(data);
    if (!result.ok) { platform_log("custom models: %s: %s",name,result.errmsg); toml_free(result); return SDL_ENUM_CONTINUE; }
    full=toml_seek(result.toptab,"full_name"); color=toml_seek(result.toptab,"colorable"); preview=toml_seek(result.toptab,"preview");
    if (full.type==TOML_STRING && *full.u.s && strlen(full.u.s)<sizeof(catalogue[0].name) &&
        color.type==TOML_BOOLEAN && preview.type==TOML_STRING && leaf_png(preview.u.s))
    {
        info=&catalogue[model_count++];
        SDL_snprintf(info->id,sizeof(info->id),"%s",name);
        SDL_snprintf(info->name,sizeof(info->name),"%s",full.u.s);
        SDL_snprintf(info->preview,sizeof(info->preview),"%s",preview.u.s);
        info->colorable=color.u.boolean;
        if (info->colorable)
        {
            default_rgb=toml_seek(result.toptab,"default_rgb");
            if (default_rgb.type==TOML_STRING && strlen(default_rgb.u.s)==7)
                memcpy(info->default_rgb,default_rgb.u.s,sizeof(info->default_rgb));
        }
    }
    else platform_log("custom models: %s: expected full_name, colorable and a local PNG preview filename",name);
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
int halo_custom_mesh_load(int index, int lod, struct halo_custom_mesh *mesh)
{
    static char const *names[]={"superlow","low","medium","high","superhigh"};
    char path[4096]; char *data; unsigned int size; int ok;
    if (index<=0 || index>=halo_custom_models_count() || lod<0 || lod>=5) return 0;
    if (SDL_snprintf(path,sizeof(path),"%s/%s/source/characters/cyborg/models/__base %s.jms",root,catalogue[index].id,names[lod])>=(int)sizeof(path)) return 0;
    data=read_bounded(path,16u*1024u*1024u,&size);
    if (!data && lod==4) { strcpy(path+strlen(path)-3,"JMS"); data=read_bounded(path,16u*1024u*1024u,&size); }
    if (!data) return 0;
    ok=halo_custom_mesh_parse(data,size,mesh); free(data);
    return ok;
}
void halo_custom_image_dispose(struct halo_custom_image *image) { free(image->pixels); memset(image,0,sizeof(*image)); }
int halo_custom_image_load(int index, int kind, struct halo_custom_image *image)
{
    char path[4096]; unsigned int size; void *data;
    unsigned char *pixels;
    int width,height,channels,w,h,x,y;
    memset(image,0,sizeof(*image));
    if (index<=0 || index>=halo_custom_models_count()) return 0;
    if (SDL_snprintf(path,sizeof(path),"%s/%s/%s",root,catalogue[index].id,
        kind==0 ? catalogue[index].preview : kind==1 ? "textures/armor.png" : "textures/armor_mask.png")>=(int)sizeof(path)) return 0;
    data=read_bounded(path,16u*1024u*1024u,&size);
    if (!data) return 0;
    if (size<8 || memcmp(data,"\x89PNG\r\n\x1a\n",8) || !stbi_info_from_memory(data,size,&width,&height,&channels) ||
        width<1 || height<1 || width>2048 || height>2048 || (kind==0 && height!=width*2)) { free(data); return 0; }
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
