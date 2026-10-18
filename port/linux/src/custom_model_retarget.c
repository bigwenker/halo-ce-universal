/* Cache rest-pose corrections, then retarget the final blended pose. No
   simulation matrices or animation clips are changed or written to disk. */
#include "halo_custom_retarget.h"
#include <math.h>
#include <string.h>
/* The port's existing SHA-256 primitive (p2p_crypto.c). */
void p2p_sha256(void const *data,int size,unsigned char *digest);

static float dot(float const *a,float const *b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static void cross(float const *a,float const *b,float *out)
{float v[3];v[0]=a[1]*b[2]-a[2]*b[1];v[1]=a[2]*b[0]-a[0]*b[2];v[2]=a[0]*b[1]-a[1]*b[0];memcpy(out,v,12);}
static float normalize(float *v)
{float n=sqrtf(dot(v,v));int i;if(n>1.e-8f)for(i=0;i<3;i++)v[i]/=n;return n;}
static void transform_vector(struct halo_custom_matrix const *m,float const *v,float *out)
{float r[3];int i;for(i=0;i<3;i++)r[i]=m->scale*(m->v[0][i]*v[0]+m->v[1][i]*v[1]+m->v[2][i]*v[2]);memcpy(out,r,12);}
void halo_custom_matrix_multiply(struct halo_custom_matrix const *a,struct halo_custom_matrix const *b,struct halo_custom_matrix *out)
{
    struct halo_custom_matrix r;int i,j;
    r.scale=a->scale*b->scale;
    for(j=0;j<3;j++)for(i=0;i<3;i++)r.v[j][i]=a->v[0][i]*b->v[j][0]+a->v[1][i]*b->v[j][1]+a->v[2][i]*b->v[j][2];
    transform_vector(a,b->v[3],r.v[3]);for(i=0;i<3;i++)r.v[3][i]+=a->v[3][i];*out=r;
}
void halo_custom_matrix_inverse(struct halo_custom_matrix const *a,struct halo_custom_matrix *out)
{
    struct halo_custom_matrix r;float p[3];int i,j;
    r.scale=1.f/a->scale;for(j=0;j<3;j++)for(i=0;i<3;i++)r.v[j][i]=a->v[i][j];
    for(i=0;i<3;i++)p[i]=-a->v[3][i];transform_vector(&r,p,r.v[3]);*out=r;
}
void halo_custom_matrix_node(struct halo_custom_node const *n,struct halo_custom_matrix *m)
{
    float x=n->rotation[0],y=n->rotation[1],z=n->rotation[2],w=n->rotation[3];
    float s=2.f/(x*x+y*y+z*z+w*w);
    memset(m,0,sizeof(*m));m->scale=1.f;
    /* Same quaternion convention as source/math/matrix_math.c. */
    m->v[0][0]=1.f-s*(y*y+z*z);m->v[0][1]=s*(x*y-w*z);m->v[0][2]=s*(x*z+w*y);
    m->v[1][0]=s*(x*y+w*z);m->v[1][1]=1.f-s*(x*x+z*z);m->v[1][2]=s*(y*z-w*x);
    m->v[2][0]=s*(x*z-w*y);m->v[2][1]=s*(y*z+w*x);m->v[2][2]=1.f-s*(x*x+y*y);
    memcpy(m->v[3],n->translation,12);
}
static int nodes_valid(struct halo_custom_node const *nodes)
{
    int i,j,at,steps;float n;
    for(i=0;i<19;i++)
    {
        if(!nodes[i].name[0] || !memchr(nodes[i].name,0,32) || nodes[i].parent< -1 || nodes[i].parent>=19)return 0;
        for(j=0;j<i;j++)if(!strcmp(nodes[i].name,nodes[j].name))return 0;
        n=0;for(j=0;j<4;j++){if(!isfinite(nodes[i].rotation[j]))return 0;n+=nodes[i].rotation[j]*nodes[i].rotation[j];}
        if(n<.999f || n>1.001f)return 0;
        for(j=0;j<3;j++)if(!isfinite(nodes[i].translation[j]) || fabsf(nodes[i].translation[j])>100.f)return 0;
        at=i;steps=0;while(at!=-1 && steps++<19)at=nodes[at].parent;if(at!=-1)return 0;
        if((i==0)!=(nodes[i].parent==-1))return 0;
    }
    return 1;
}
static void put_u32(unsigned char **p,unsigned int v)
{int i;for(i=0;i<4;i++){*(*p)++=(unsigned char)v;v>>=8;}}
static unsigned int get_u32(unsigned char const **p)
{unsigned int v=0;int i;for(i=0;i<4;i++)v|=(unsigned int)*(*p)++<<(i*8);return v;}
static void put_float(unsigned char **p,float v)
{unsigned int bits;if(v==0.f)v=0.f;memcpy(&bits,&v,4);put_u32(p,bits);}
static float get_float(unsigned char const **p)
{unsigned int bits=get_u32(p);float v;memcpy(&v,&bits,4);return v;}
/* Hash only the parsed skeleton, including its index order (cached matrices
   use that order). Whitespace, geometry, textures and quaternion sign do not
   invalidate the key. Serialize explicitly, never hash structure padding. */
int halo_custom_skeleton_hash(struct halo_custom_node const *s,struct halo_custom_node const *t,unsigned char hash[32])
{
    unsigned char bytes[4096],*p=bytes;int pass,i,j,n;float q[4],length,sign;
    if(!nodes_valid(s) || !nodes_valid(t))return 0;
    put_u32(&p,HALO_RETARGET_VERSION);
    for(pass=0;pass<2;pass++)
    {
        struct halo_custom_node const *nodes=pass ? t:s;
        for(i=0;i<19;i++)
        {
            struct halo_custom_node const *node=&nodes[i];
            n=(int)strlen(node->name)+1;memcpy(p,node->name,n);p+=n;
            if(node->parent>=0){n=(int)strlen(nodes[node->parent].name)+1;memcpy(p,nodes[node->parent].name,n);p+=n;}else *p++=0;
            length=0;for(j=0;j<4;j++)length+=node->rotation[j]*node->rotation[j];length=sqrtf(length);
            sign=1;for(j=3;j>=0;j--)if(node->rotation[j]!=0){if(node->rotation[j]<0)sign=-1;break;}
            for(j=0;j<4;j++)q[j]=node->rotation[j]*sign/length;
            for(j=0;j<4;j++)put_float(&p,q[j]);for(j=0;j<3;j++)put_float(&p,node->translation[j]);
        }
    }
    p2p_sha256(bytes,(int)(p-bytes),hash);return 1;
}
int halo_custom_retarget_build(struct halo_custom_node const *s,struct halo_custom_node const *t,struct halo_custom_retarget *p)
{
    struct halo_custom_matrix source[19],local;int i,j,done[19]={0},left=19,progress;
    memset(p,0,sizeof(*p));if(!nodes_valid(s) || !nodes_valid(t))return 0;
    for(i=0;i<19;i++)
    {
        for(j=0;j<19;j++)if(!strcmp(t[i].name,s[j].name))break;if(j==19)return 0;p->map[i]=j;
    }
    for(i=0;i<19;i++)
    {j=p->map[i];p->parent[j]=s[j].parent;if(t[i].parent<0 ? s[j].parent!=-1 : p->map[t[i].parent]!=s[j].parent)return 0;}
    while(left)
    {
        progress=0;
        for(i=0;i<19;i++)
        {
            int at=p->map[i],parent=p->parent[at];
            if(done[at] || (parent>=0 && !done[parent]))continue;
            halo_custom_matrix_node(&s[at],&local);if(parent>=0)halo_custom_matrix_multiply(&source[parent],&local,&source[at]);else source[at]=local;
            halo_custom_matrix_node(&t[i],&local);if(parent>=0)halo_custom_matrix_multiply(&p->target[parent],&local,&p->target[at]);else p->target[at]=local;
            halo_custom_matrix_inverse(&source[at],&p->source_inverse[at]);halo_custom_matrix_inverse(&p->target[at],&p->target_inverse[at]);
            done[at]=1;left--;progress=1;
        }
        if(!progress)return 0;
    }
    p->identical=1;
    for(i=0;i<19;i++)for(j=0;j<12;j++)if(fabsf(((float *)source[i].v)[j]-((float *)p->target[i].v)[j])>1.e-5f)p->identical=0;
    return 1;
}
static int matrix_valid(struct halo_custom_matrix const *m)
{
    int i;float c[3];
    if(!isfinite(m->scale) || m->scale<.001f || m->scale>100.f)return 0;
    for(i=0;i<12;i++)if(!isfinite(((float const *)m->v)[i]) || fabsf(((float const *)m->v)[i])>100000.f)return 0;
    for(i=0;i<3;i++)if(fabsf(dot(m->v[i],m->v[i])-1.f)>.01f)return 0;
    cross(m->v[0],m->v[1],c);return fabsf(dot(c,m->v[2])-1.f)<.01f;
}
int halo_custom_retarget_pose(struct halo_custom_retarget const *p,struct halo_custom_matrix const *s,struct halo_custom_matrix *t)
{
    struct halo_custom_matrix delta[19],source_bind,inv,local;int done[19]={0},left=19,i,j,parent,progress;float edge[3],motion[3],v[3];
    for(i=0;i<19;i++)if(!matrix_valid(&s[i]))return 0;
    if(p->identical){memcpy(t,s,19*sizeof(*t));return 1;}
    for(i=0;i<19;i++)halo_custom_matrix_multiply(&s[i],&p->source_inverse[i],&delta[i]);
    while(left)
    {
        progress=0;
        for(i=0;i<19;i++)
        {
            parent=p->parent[i];if(done[i] || (parent>=0 && !done[parent]))continue;
            halo_custom_matrix_multiply(&delta[i],&p->target[i],&t[i]);
            if(parent>=0)
            {
                /* Keep the target's rest length/direction. Preserve animated
                   local translations (overlays, vehicles) in source axes. */
                for(j=0;j<3;j++)edge[j]=p->target[i].v[3][j]-p->target[parent].v[3][j];
                transform_vector(&delta[parent],edge,v);
                halo_custom_matrix_inverse(&s[parent],&inv);halo_custom_matrix_multiply(&inv,&s[i],&local);
                halo_custom_matrix_inverse(&p->source_inverse[i],&source_bind);
                halo_custom_matrix_multiply(&p->source_inverse[parent],&source_bind,&inv);
                for(j=0;j<3;j++)motion[j]=local.v[3][j]-inv.v[3][j];
                transform_vector(&s[parent],motion,motion);
                for(j=0;j<3;j++)t[i].v[3][j]=t[parent].v[3][j]+v[j]+motion[j];
            }
            if(!matrix_valid(&t[i]))return 0;done[i]=1;left--;progress=1;
        }
        if(!progress)return 0;
    }
    return 1;
}
/* Rotate an existing joint's basis by the shortest arc between its old/new
   limb vectors. Unlike resetting its forward axis this retains authored roll. */
static void aim_segment(struct halo_custom_matrix *m,float const *old,float const *new_direction)
{
    float a[3],b[3],axis[3],c,s,tmp[3],proj;int i,j;
    memcpy(a,old,12);memcpy(b,new_direction,12);if(normalize(a)<1.e-8f || normalize(b)<1.e-8f)return;
    c=dot(a,b);if(c>1.f)c=1.f;if(c< -1.f)c=-1.f;cross(a,b,axis);s=normalize(axis);
    if(s<1.e-6f)
    {if(c>0)return;memset(tmp,0,12);tmp[fabsf(a[0])<.8f ? 0:1]=1;cross(a,tmp,axis);normalize(axis);s=0;}
    for(j=0;j<3;j++)
    {cross(axis,m->v[j],tmp);proj=dot(axis,m->v[j]);for(i=0;i<3;i++)m->v[j][i]=m->v[j][i]*c+tmp[i]*s+axis[i]*proj*(1-c);}
}
int halo_custom_retarget_arm(struct halo_custom_matrix *a,struct halo_custom_matrix *b,struct halo_custom_matrix *h,struct halo_custom_matrix const *goal)
{
    float upper[3],lower[3],direction[3],bend[3],new_upper[3],new_lower[3],end[3],length1,length2,d,projection,height,along,min,max;int i;
    if(!matrix_valid(a)||!matrix_valid(b)||!matrix_valid(h)||!matrix_valid(goal))return 0;
    for(i=0;i<3;i++){upper[i]=b->v[3][i]-a->v[3][i];lower[i]=h->v[3][i]-b->v[3][i];direction[i]=goal->v[3][i]-a->v[3][i];}
    length1=sqrtf(dot(upper,upper));length2=sqrtf(dot(lower,lower));d=normalize(direction);
    if(length1<1.e-5f || length2<1.e-5f || d<1.e-6f)return 0;
    min=fabsf(length1-length2)+1.e-6f;max=length1+length2-1.e-6f;if(d<min)d=min;if(d>max)d=max;
    along=dot(upper,direction);for(i=0;i<3;i++)bend[i]=upper[i]-along*direction[i];
    if(normalize(bend)<1.e-6f)
    {cross(direction,a->v[2],bend);if(normalize(bend)<1.e-6f){cross(direction,a->v[1],bend);if(normalize(bend)<1.e-6f)return 0;}}
    projection=(d*d+length1*length1-length2*length2)/(2*d);height=length1*length1-projection*projection;height=sqrtf(height>0 ? height:0);
    for(i=0;i<3;i++){new_upper[i]=projection*direction[i]+height*bend[i];end[i]=a->v[3][i]+direction[i]*d;new_lower[i]=end[i]-a->v[3][i]-new_upper[i];}
    aim_segment(a,upper,new_upper);aim_segment(b,lower,new_lower);
    for(i=0;i<3;i++)b->v[3][i]=a->v[3][i]+new_upper[i];*h=*goal;memcpy(h->v[3],end,12);return 1;
}
/* Explicit little-endian representation: no ABI padding, pointers or host
   endianness. A digest, bounds and rigid-matrix checks reject corrupt caches. */
int halo_custom_retarget_encode(struct halo_custom_retarget const *plan,unsigned char const key[32],unsigned char data[HALO_RETARGET_CACHE_BYTES])
{
    unsigned char *p=data;int i,j,k;unsigned char digest[32];
    memcpy(p,"HCERTGT1",8);p+=8;put_u32(&p,HALO_RETARGET_VERSION);memcpy(p,key,32);p+=32;
    put_u32(&p,plan->identical);
    for(i=0;i<19;i++)
    {
        struct halo_custom_matrix const *m[3]={&plan->source_inverse[i],&plan->target[i],&plan->target_inverse[i]};
        put_u32(&p,(unsigned int)(plan->parent[i]+1));put_u32(&p,plan->map[i]);
        for(j=0;j<3;j++){put_float(&p,m[j]->scale);for(k=0;k<12;k++)put_float(&p,((float const *)m[j]->v)[k]);}
    }
    p2p_sha256(data,(int)(p-data),digest);memcpy(p,digest,32);p+=32;return (int)(p-data);
}
int halo_custom_retarget_decode(unsigned char const *data,unsigned int size,unsigned char const key[32],struct halo_custom_retarget *plan)
{
    unsigned char const *p=data;unsigned char digest[32];struct halo_custom_retarget r;struct halo_custom_matrix check;
    unsigned int parent,map;int i,j,k,at,steps,seen[19]={0};
    if(size!=3196 || memcmp(p,"HCERTGT1",8))return 0;p+=8;if(get_u32(&p)!=HALO_RETARGET_VERSION || memcmp(p,key,32))return 0;p+=32;
    p2p_sha256(data,(int)size-32,digest);if(memcmp(digest,data+size-32,32))return 0;
    memset(&r,0,sizeof(r));r.identical=(int)get_u32(&p);if(r.identical!=0 && r.identical!=1)return 0;
    for(i=0;i<19;i++)
    {
        struct halo_custom_matrix *m[3]={&r.source_inverse[i],&r.target[i],&r.target_inverse[i]};
        parent=get_u32(&p);map=get_u32(&p);if(parent>19 || map>=19 || seen[map]++)return 0;
        r.parent[i]=(int)parent-1;r.map[i]=(int)map;
        for(j=0;j<3;j++){m[j]->scale=get_float(&p);for(k=0;k<12;k++)((float *)m[j]->v)[k]=get_float(&p);if(!matrix_valid(m[j]))return 0;}
        halo_custom_matrix_multiply(&r.target[i],&r.target_inverse[i],&check);
        for(j=0;j<12;j++)if(fabsf(((float *)check.v)[j]-(j<9 && j%4==0 ? 1.f:0.f))>.001f)return 0;
        if(r.identical)
        {
            halo_custom_matrix_multiply(&r.target[i],&r.source_inverse[i],&check);
            for(j=0;j<12;j++)if(fabsf(((float *)check.v)[j]-(j<9 && j%4==0 ? 1.f:0.f))>.001f)return 0;
        }
    }
    for(i=0;i<19;i++)
    {at=i;steps=0;while(at!=-1 && steps++<19)at=r.parent[at];if(at!=-1 || ((i==0)!=(r.parent[i]==-1)))return 0;}
    *plan=r;return 1;
}
