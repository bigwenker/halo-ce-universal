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
    if(ok)
    {
        int i;
        printf("%d %d\n",mesh.vertex_count,mesh.triangle_count);
        for(i=0;i<mesh.vertex_count && i<1000;i++)
        {
            struct halo_custom_vertex const *v=&mesh.vertices[i];
            printf("v %g %g %g %g %g %g %g %g\n",v->position[0],v->position[1],v->position[2],
                v->normal[0],v->normal[1],v->normal[2],v->uv[0],v->uv[1]);
        }
        for(i=0;i<mesh.triangle_count && i<1000;i++)
            printf("t %u %u %u\n",mesh.triangles[i*3],mesh.triangles[i*3+1],mesh.triangles[i*3+2]);
        for(i=0;i<mesh.material_count;i++)
        {
            struct halo_custom_mesh part;
            printf("m %d %s %s\n",i,mesh.materials[i].name,mesh.materials[i].texture);
            if(!halo_custom_mesh_material(&mesh,i,&part)){halo_custom_mesh_dispose(&mesh);free(data);return 2;}
            printf("p %d %d %d\n",i,part.vertex_count,part.triangle_count);
            for(int j=0;j<part.triangle_count*3;j++)CHECK(part.triangles[j]<(unsigned int)part.vertex_count,"part index escaped local buffer");
            halo_custom_mesh_dispose(&part);
        }
        for(i=0;i<mesh.triangle_count && i<1000;i++)printf("a %d %u\n",i,mesh.triangle_materials[i]);
        halo_custom_mesh_dispose(&mesh);
    }
    free(data);return ok ? 0:1;
}
