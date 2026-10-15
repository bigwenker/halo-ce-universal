#include "harness.h"
#include <stdio.h>
#include <stdlib.h>
#include "halo_custom_models.h"
#include "jms.inc"
int main(int argc,char **argv)
{
    FILE *file;long size;char *data;struct halo_custom_mesh mesh;int ok;
    CHECK(argc==2,"input file");file=fopen(argv[1],"rb");CHECK(file,"open");
    fseek(file,0,SEEK_END);size=ftell(file);rewind(file);data=malloc(size+1);
    CHECK(data && fread(data,1,size,file)==(size_t)size,"read");fclose(file);
    ok=halo_custom_mesh_parse(data,(unsigned int)size,&mesh);
    if(ok){printf("%d %d\n",mesh.vertex_count,mesh.triangle_count);halo_custom_mesh_dispose(&mesh);}
    free(data);return ok ? 0:1;
}
