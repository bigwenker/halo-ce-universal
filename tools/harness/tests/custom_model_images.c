#include "harness.h"
#include <strings.h>
#include "halo_custom_models.h"
#define SDL_snprintf snprintf
#define SDL_strcasecmp strcasecmp
static char root[4096];
static struct halo_custom_model_info catalogue[2];
int halo_custom_models_count(void){return 2;}
#include "images.inc"
int main(int argc,char **argv){struct halo_custom_image image;int kind,i;CHECK(argc==3 || argc==4,"root, kind and optional material");snprintf(root,sizeof(root),"%s",argv[1]);strcpy(catalogue[1].id,"model");strcpy(catalogue[1].preview,"preview.bmp");kind=atoi(argv[2]);
 if(!(argc==4 ? halo_custom_material_image_load(1,argv[3],kind==2,&image) : halo_custom_image_load(1,kind,&image)))return 1;
 printf("%d %d",image.width,image.height);
 for(i=0;i<3;i++){int x=i==0?0:i==1?image.width/2:image.width-1,y=i==0?0:i==1?image.height/2:image.height-1;printf(" %08x",image.pixels[y*image.width+x]);}
 printf("\n");halo_custom_image_dispose(&image);return 0;}
