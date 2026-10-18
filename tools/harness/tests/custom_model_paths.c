#include "harness.h"
#include <math.h>
#include "halo_custom_models.h"
#define SDL_snprintf snprintf
static char root[4096];
static struct halo_custom_model_info catalogue[2];
int halo_custom_models_count(void) {return 2;}
#include "jms.inc"
#include "paths.inc"
int main(int argc,char **argv)
{
    struct halo_custom_mesh mesh;int ok;
    if(argc!=3)return 2;snprintf(root,sizeof(root),"%s",argv[1]);strcpy(catalogue[1].id,"model");
    ok=halo_custom_mesh_load(1,atoi(argv[2]),&mesh);if(!ok)return 1;
    printf("%.6f\n",mesh.vertices[0].position[0]);halo_custom_mesh_dispose(&mesh);return 0;
}
