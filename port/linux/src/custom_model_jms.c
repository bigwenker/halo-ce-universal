/* Strict, bounded JMS 8200 reader for the Chief skeleton and one armor
   material. These are authoring inputs, never packets or executable tags. */
#include "halo_custom_models.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
struct reader { char const *at, *end; int failed; };
static void space(struct reader *r) { while(r->at<r->end && (*r->at==' ' || *r->at=='\t' || *r->at=='\r' || *r->at=='\n')) r->at++; }
static double number(struct reader *r)
{
    char token[64],*end; size_t n=0; double v;
    space(r);
    while(r->at<r->end && *r->at!=' ' && *r->at!='\t' && *r->at!='\r' && *r->at!='\n')
    { if(n+1<sizeof(token)) token[n++]=*r->at; else r->failed=1; r->at++; }
    token[n]=0; v=strtod(token,&end);
    if (!n || *end || !isfinite(v) || v < -2147483648.0 || v > 4294967295.0) {r->failed=1;return 0;}
    return v;
}
static int integer(struct reader *r, int low, int high)
{
    double v=number(r);
    if (v<low || v>high || v!=(int)v) {r->failed=1;return low;}
    return (int)v;
}
static float real_value(struct reader *r, float low, float high)
{ double v=number(r);if(v<low || v>high) {r->failed=1;return 0;}return (float)v; }
static void line(struct reader *r, char *text, size_t capacity)
{
    size_t n=0;
    space(r);
    while(r->at<r->end && *r->at!='\r' && *r->at!='\n')
    {if(n+1<capacity)text[n++]=*r->at;else r->failed=1;r->at++;}
    while(n && (text[n-1]==' ' || text[n-1]=='\t')) n--;
    text[n]=0;
    if (!n) r->failed=1;
}
void halo_custom_mesh_dispose(struct halo_custom_mesh *m)
{free(m->vertices);free(m->triangles);memset(m,0,sizeof(*m));}
int halo_custom_mesh_parse(char const *data, unsigned int size, struct halo_custom_mesh *m)
{
    struct reader r;
    int i,j,n,regions,parents[19]; char name[512];
    memset(m,0,sizeof(*m));
    if (!data || !size || size>16u*1024u*1024u || memchr(data,0,size)) return 0;
    r.at=data;r.end=data+size;r.failed=0;
    if(integer(&r,8200,8200)!=8200) return 0;
    number(&r); /* authoring checksum; runtime uses the validated stock nodes */
    if(integer(&r,19,19)!=19 || r.failed) return 0;
    for(i=0;i<19;i++)
    {
        struct halo_custom_node *node=&m->nodes[i];
        line(&r,node->name,sizeof(node->name));
        node->child=integer(&r,-1,18);node->sibling=integer(&r,-1,18);node->parent=-1;parents[i]=-1;
        {
            float norm=0;
            for(j=0;j<4;j++){node->rotation[j]=real_value(&r,-1.001f,1.001f);norm+=node->rotation[j]*node->rotation[j];}
            if(norm<.999f || norm>1.001f)r.failed=1;
        }
        for(j=0;j<3;j++)node->translation[j]=real_value(&r,-10000.f,10000.f)/100.f;
    }
    for(i=0;i<19;i++)
    {
        int at=m->nodes[i].child,steps=0;
        while(at!=-1 && steps++<19)
        {if(at==0 || parents[at]!=-1)r.failed=1;parents[at]=i;at=m->nodes[at].sibling;}
        if(steps>19)r.failed=1;
    }
    for(i=0;i<19;i++)
    {
        int at=i,steps=0;
        m->nodes[i].parent=parents[i];
        while(at!=-1 && steps++<20)at=parents[at];
        if(steps>19 || (i && parents[i]==-1))r.failed=1;
    }
    if(m->nodes[0].sibling!=-1)r.failed=1;
    if(integer(&r,1,1)!=1)r.failed=1;
    line(&r,name,sizeof(name));if(strcmp(name,"armor"))r.failed=1;
    line(&r,name,sizeof(name)); /* bitmap hint is replaced by textures/armor.png */
    n=integer(&r,0,256);
    for(i=0;i<n;i++)
    {
        line(&r,name,sizeof(name));integer(&r,-1,31);integer(&r,-1,18);
        for(j=0;j<4;j++)real_value(&r,-1.001f,1.001f);
        for(j=0;j<3;j++)real_value(&r,-10000.f,10000.f);
        real_value(&r,0,10000.f);
    }
    regions=integer(&r,1,8);
    for(i=0;i<regions;i++)line(&r,name,sizeof(name));
    m->vertex_count=integer(&r,3,65535);
    if(r.failed)goto fail;
    m->vertices=calloc(m->vertex_count,sizeof(*m->vertices));
    if(!m->vertices)goto fail;
    for(i=0;i<m->vertex_count;i++)
    {
        struct halo_custom_vertex *v=&m->vertices[i];
        float length=0;
        v->node[0]=integer(&r,0,18);
        for(j=0;j<3;j++)v->position[j]=real_value(&r,-10000.f,10000.f)/100.f;
        for(j=0;j<3;j++){v->normal[j]=real_value(&r,-1.001f,1.001f);length+=v->normal[j]*v->normal[j];}
        if(length<.9f || length>1.1f)r.failed=1;
        v->node[1]=integer(&r,-1,18);v->weight=real_value(&r,0,1);
        if(v->node[1]==-1 && v->weight!=0)r.failed=1;
        v->uv[0]=real_value(&r,-16,16);v->uv[1]=1.f-real_value(&r,-16,16);real_value(&r,-16,16);
    }
    m->triangle_count=integer(&r,1,65535);
    if(r.failed)goto fail;
    m->triangles=malloc((size_t)m->triangle_count*3*sizeof(*m->triangles));
    if(!m->triangles)goto fail;
    for(i=0;i<m->triangle_count;i++)
    {
        integer(&r,0,regions-1);integer(&r,0,0);
        /* port: JMS faces wind opposite native model buffers. Reverse the
           face, preserving its authored normals and per-vertex attributes. */
        m->triangles[i*3]=(unsigned short)integer(&r,0,m->vertex_count-1);
        m->triangles[i*3+2]=(unsigned short)integer(&r,0,m->vertex_count-1);
        m->triangles[i*3+1]=(unsigned short)integer(&r,0,m->vertex_count-1);
    }
    space(&r);
    if(r.failed || r.at!=r.end)goto fail;
    return 1;
fail:halo_custom_mesh_dispose(m);return 0;
}
