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
static void aim_segment(struct halo_custom_matrix *m,float const *old,float const *new_direction);
static void rotation_only(struct halo_custom_matrix *m)
{m->scale=1.f;memset(m->v[3],0,12);}
static void identity_matrix(struct halo_custom_matrix *m)
{int i;memset(m,0,sizeof(*m));m->scale=1;for(i=0;i<3;i++)m->v[i][i]=1;}
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
static int nodes_valid(int count,struct halo_custom_node const *nodes)
{
    int i,j,at,steps;float n;
    if(count<1 || count>HALO_CUSTOM_MAX_NODES)return 0;
    for(i=0;i<count;i++)
    {
        if(!nodes[i].name[0] || !memchr(nodes[i].name,0,32) || nodes[i].parent< -1 || nodes[i].parent>=count)return 0;
        for(j=0;j<i;j++)if(!strcmp(nodes[i].name,nodes[j].name))return 0;
        n=0;for(j=0;j<4;j++){if(!isfinite(nodes[i].rotation[j]))return 0;n+=nodes[i].rotation[j]*nodes[i].rotation[j];}
        if(n<.999f || n>1.001f)return 0;
        for(j=0;j<3;j++)if(!isfinite(nodes[i].translation[j]) || fabsf(nodes[i].translation[j])>100.f)return 0;
        if((i==0)!=(nodes[i].parent==-1))return 0;
    }
    for(i=0;i<count;i++){at=i;steps=0;while(at!=-1 && steps++<count)at=nodes[at].parent;if(at!=-1)return 0;}
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
int halo_custom_skeleton_hash_count(int count,struct halo_custom_node const *s,struct halo_custom_node const *t,unsigned char hash[32])
{
    unsigned char bytes[HALO_RETARGET_CACHE_BYTES],*p=bytes;int pass,i,j,n;float q[4],length,sign;
    if(!nodes_valid(count,s) || !nodes_valid(count,t))return 0;
    put_u32(&p,HALO_RETARGET_VERSION);put_u32(&p,(unsigned int)count);
    for(pass=0;pass<2;pass++)
    {
        struct halo_custom_node const *nodes=pass ? t:s;
        for(i=0;i<count;i++)
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
int halo_custom_retarget_build_count(int count,struct halo_custom_node const *s,struct halo_custom_node const *t,struct halo_custom_retarget *p)
{
    struct halo_custom_matrix source[HALO_CUSTOM_MAX_NODES],local,alignment,inverse;int i,j,child,done[HALO_CUSTOM_MAX_NODES]={0},left=count,progress;
    float a[3],b[3],length,best;
    static char const *chains[][2]={{"bip01 pelvis","bip01 spine"},{"bip01 spine","bip01 spine1"},{"bip01 spine1","bip01 neck"},{"bip01 neck","bip01 head"}};
    memset(p,0,sizeof(*p));if(!nodes_valid(count,s) || !nodes_valid(count,t))return 0;
    p->count=count;
    for(i=0;i<count;i++)
    {
        for(j=0;j<count;j++)if(!strcmp(t[i].name,s[j].name))break;if(j==count)return 0;p->map[i]=j;
    }
    for(i=0;i<count;i++)
    {j=p->map[i];p->parent[j]=s[j].parent;if(t[i].parent<0 ? s[j].parent!=-1 : p->map[t[i].parent]!=s[j].parent)return 0;}
    while(left)
    {
        progress=0;
        for(i=0;i<count;i++)
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
    for(i=0;i<count;i++)for(j=0;j<12;j++)if(fabsf(((float *)source[i].v)[j]-((float *)p->target[i].v)[j])>1.e-5f)p->identical=0;
    for(i=0;i<count;i++)
    {
        /* Exporters may move joints without realigning their bone axes.
           Calibrate the motion axes from the actual segments, preserving
           exported roll. Prefer the torso chain over its branching limbs. */
        child=-1;best=0;
        for(j=0;j<count;j++)if(p->parent[j]==i)
        {
            int k;for(k=0;k<3;k++)a[k]=source[j].v[3][k]-source[i].v[3][k];length=dot(a,a);
            if(length>best){best=length;child=j;}
        }
        for(j=0;j<4;j++)if(!strcmp(s[i].name,chains[j][0]))
        {int k;for(k=0;k<count;k++)if(p->parent[k]==i && !strcmp(s[k].name,chains[j][1])){child=k;break;}}
        identity_matrix(&alignment);
        if(child<0)child=p->parent[i];
        if(child>=0)
        {
            for(j=0;j<3;j++){a[j]=source[child].v[3][j]-source[i].v[3][j];b[j]=p->target[child].v[3][j]-p->target[i].v[3][j];}
            aim_segment(&alignment,a,b);
        }
        inverse=p->target_inverse[i];rotation_only(&inverse);
        local=source[i];rotation_only(&local);
        halo_custom_matrix_multiply(&inverse,&alignment,&inverse);
        halo_custom_matrix_multiply(&inverse,&local,&p->rotation_map[i]);
    }
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
    struct halo_custom_matrix bind,inv,local,delta,mapped,target_local;
    int done[HALO_CUSTOM_MAX_NODES]={0},count=p->count,left=count,i,j,parent,progress;float motion[3];
    if(count<1 || count>HALO_CUSTOM_MAX_NODES)return 0;
    for(i=0;i<count;i++)if(!matrix_valid(&s[i]))return 0;
    if(p->identical){memcpy(t,s,count*sizeof(*t));return 1;}
    while(left)
    {
        progress=0;
        for(i=0;i<count;i++)
        {
            parent=p->parent[i];if(done[i] || (parent>=0 && !done[parent]))continue;
            if(parent<0)
            {
                /* World placement, locomotion and instance scale belong to
                   the game. Only joint-local motion is retargeted below. */
                halo_custom_matrix_multiply(&s[i],&p->source_inverse[i],&delta);
                halo_custom_matrix_multiply(&delta,&p->target[i],&t[i]);
            }
            else
            {
                halo_custom_matrix_inverse(&s[parent],&inv);halo_custom_matrix_multiply(&inv,&s[i],&local);
                halo_custom_matrix_inverse(&p->source_inverse[i],&bind);
                halo_custom_matrix_multiply(&p->source_inverse[parent],&bind,&bind);
                for(j=0;j<3;j++)motion[j]=local.v[3][j]-bind.v[3][j];
                halo_custom_matrix_inverse(&bind,&inv);halo_custom_matrix_multiply(&inv,&local,&delta);memset(delta.v[3],0,12);
                halo_custom_matrix_inverse(&p->rotation_map[i],&inv);
                halo_custom_matrix_multiply(&p->rotation_map[i],&delta,&mapped);halo_custom_matrix_multiply(&mapped,&inv,&mapped);
                halo_custom_matrix_multiply(&p->target_inverse[parent],&p->target[i],&target_local);
                halo_custom_matrix_multiply(&target_local,&mapped,&target_local);
                transform_vector(&p->rotation_map[parent],motion,motion);
                for(j=0;j<3;j++)target_local.v[3][j]+=motion[j];
                halo_custom_matrix_multiply(&t[parent],&target_local,&t[i]);
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
/* Resolve the bend in the original reach plane, then parallel-transport it
   to the new reach direction. Projecting the old elbow onto the NEW reach
   line instead flips the elbow when that line passes the upper arm. */
static void transport_pole(float const *from,float const *to,float *pole)
{
    struct halo_custom_matrix rotation;float along;int i;
    identity_matrix(&rotation);aim_segment(&rotation,from,to);transform_vector(&rotation,pole,pole);
    along=dot(pole,to);for(i=0;i<3;i++)pole[i]-=along*to[i];
    normalize(pole);
}
static void limb_pole(struct halo_custom_matrix const *a,struct halo_custom_matrix const *b,struct halo_custom_matrix const *h,float *direction,float *pole)
{
    float upper[3],along;int i,axis=0;
    for(i=0;i<3;i++){direction[i]=h->v[3][i]-a->v[3][i];upper[i]=b->v[3][i]-a->v[3][i];}
    if(normalize(direction)<1.e-8f){memcpy(direction,a->v[0],12);normalize(direction);}
    along=dot(upper,direction);for(i=0;i<3;i++)pole[i]=upper[i]-along*direction[i];
    if(normalize(pole)<1.e-6f)
    {
        /* A completely straight limb has no bend plane. Use its authored
           basis, never a world axis or the noise of a tiny target offset. */
        for(i=1;i<3;i++)if(fabsf(dot(a->v[i],direction))<fabsf(dot(a->v[axis],direction)))axis=i;
        along=dot(a->v[axis],direction);for(i=0;i<3;i++)pole[i]=a->v[axis][i]-along*direction[i];normalize(pole);
    }
}
static int arm_solve(struct halo_custom_matrix *a,struct halo_custom_matrix *b,struct halo_custom_matrix *h,struct halo_custom_matrix const *goal,float const *pole)
{
    float upper[3],lower[3],direction[3],bend[3],new_upper[3],new_lower[3],end[3],length1,length2,d,projection,height,along,min,max;int i;
    if(!matrix_valid(a)||!matrix_valid(b)||!matrix_valid(h)||!matrix_valid(goal))return 0;
    for(i=0;i<3;i++){upper[i]=b->v[3][i]-a->v[3][i];lower[i]=h->v[3][i]-b->v[3][i];direction[i]=goal->v[3][i]-a->v[3][i];}
    length1=sqrtf(dot(upper,upper));length2=sqrtf(dot(lower,lower));d=normalize(direction);
    if(length1<1.e-5f || length2<1.e-5f || d<1.e-6f)return 0;
    /* Two to 160 degrees of flexion: no locked-straight elbow or folded
       joint at an unreachable grip. Preserve lengths; clamp the contact. */
    min=sqrtf(length1*length1+length2*length2+2*length1*length2*(-.93969262f));
    max=sqrtf(length1*length1+length2*length2+2*length1*length2*.999390827f);
    if(d<min)d=min;if(d>max)d=max;
    along=dot(pole,direction);for(i=0;i<3;i++)bend[i]=pole[i]-along*direction[i];
    if(normalize(bend)<1.e-6f)return 0;
    projection=(d*d+length1*length1-length2*length2)/(2*d);height=length1*length1-projection*projection;height=sqrtf(height>0 ? height:0);
    for(i=0;i<3;i++){new_upper[i]=projection*direction[i]+height*bend[i];end[i]=a->v[3][i]+direction[i]*d;new_lower[i]=end[i]-a->v[3][i]-new_upper[i];}
    aim_segment(a,upper,new_upper);aim_segment(b,lower,new_lower);
    for(i=0;i<3;i++)b->v[3][i]=a->v[3][i]+new_upper[i];*h=*goal;memcpy(h->v[3],end,12);return 1;
}
int halo_custom_retarget_arm(struct halo_custom_matrix *a,struct halo_custom_matrix *b,struct halo_custom_matrix *h,struct halo_custom_matrix const *goal)
{
    float from[3],to[3],pole[3];int i;
    limb_pole(a,b,h,from,pole);for(i=0;i<3;i++)to[i]=goal->v[3][i]-a->v[3][i];
    if(normalize(to)<1.e-8f)return 0;transport_pole(from,to,pole);return arm_solve(a,b,h,goal,pole);
}
/* Retarget source elbow/knee intent, including a stable rest reference for
   nearly straight animations. The calibrated joint map also handles rigs
   whose exported rotation axes do not follow their edited joint positions. */
static void contact_pole(struct halo_custom_retarget const *p,struct halo_custom_matrix const *s,struct halo_custom_matrix const *t,int upper,int forearm,int wrist,float const *to,float *pole)
{
    struct halo_custom_matrix rest[3],motion,inverse;float from[3],reference[3],rest_direction[3],current[3],upper_vector[3],along,weight;int i;
    halo_custom_matrix_inverse(&p->source_inverse[upper],&rest[0]);
    halo_custom_matrix_inverse(&p->source_inverse[forearm],&rest[1]);
    halo_custom_matrix_inverse(&p->source_inverse[wrist],&rest[2]);
    limb_pole(&rest[0],&rest[1],&rest[2],rest_direction,reference);
    halo_custom_matrix_multiply(&s[upper],&p->source_inverse[upper],&motion);rotation_only(&motion);transform_vector(&motion,reference,reference);transform_vector(&motion,rest_direction,rest_direction);normalize(rest_direction);
    for(i=0;i<3;i++){from[i]=s[wrist].v[3][i]-s[upper].v[3][i];upper_vector[i]=s[forearm].v[3][i]-s[upper].v[3][i];}
    if(normalize(from)<1.e-8f)memcpy(from,rest_direction,12);
    transport_pole(rest_direction,from,reference);
    along=dot(upper_vector,from);for(i=0;i<3;i++)current[i]=upper_vector[i]-along*from[i];
    weight=normalize(current)/(.1f*sqrtf(dot(upper_vector,upper_vector))+1.e-8f);
    /* Near extension, keep the anatomical bend side rather than amplifying
       tiny animation/blending errors into a half-turn at the elbow. */
    if(weight>1)weight=1;
    /* Fade an opposing pole back to the anatomical reference continuously;
       a sign flip or a hard hemisphere threshold produces another pop. */
    along=dot(current,reference);if(along>1)along=1;if(along< -1)along=-1;weight*=.5f*(1+along);
    for(i=0;i<3;i++)pole[i]=reference[i]*(1-weight)+current[i]*weight;normalize(pole);
    halo_custom_matrix_inverse(&s[upper],&inverse);rotation_only(&inverse);
    halo_custom_matrix_multiply(&p->rotation_map[upper],&inverse,&motion);
    halo_custom_matrix_multiply(&t[upper],&motion,&motion);rotation_only(&motion);
    transform_vector(&motion,pole,pole);transform_vector(&motion,from,from);normalize(from);
    transport_pole(from,to,pole);
}
int halo_custom_retarget_contact(struct halo_custom_retarget const *p,struct halo_custom_matrix const *s,struct halo_custom_matrix *t,int clavicle,int upper,int forearm,int wrist,struct halo_custom_matrix const *goal)
{
    struct halo_custom_matrix old[HALO_CUSTOM_MAX_NODES],inverse,delta,shoulder;
    float from[3],to[3],pole[3],reach[3],length1,length2,d,cosine,angle,fraction;int i,j,at,steps,changed[HALO_CUSTOM_MAX_NODES]={0};
    if(p->count<1 || p->count>HALO_CUSTOM_MAX_NODES || upper<0 || upper>=p->count || forearm<0 || forearm>=p->count || wrist<0 || wrist>=p->count ||
        p->parent[forearm]!=upper || p->parent[wrist]!=forearm)return 0;
    if(clavicle>=p->count || (clavicle>=0 && p->parent[upper]!=clavicle) || !matrix_valid(goal))return 0;
    for(i=0;i<p->count;i++)if(!matrix_valid(&s[i]) || !matrix_valid(&t[i]))return 0;
    memcpy(old,t,p->count*sizeof(*old));
    if(clavicle>=0)
    {
        for(i=0;i<3;i++){from[i]=t[forearm].v[3][i]-t[upper].v[3][i];to[i]=t[wrist].v[3][i]-t[forearm].v[3][i];reach[i]=goal->v[3][i]-t[upper].v[3][i];}
        length1=sqrtf(dot(from,from));length2=sqrtf(dot(to,to));d=sqrtf(dot(reach,reach));
        if(d>.95f*(length1+length2) && length1+length2>1.e-5f)
        {
            for(i=0;i<3;i++){from[i]=t[upper].v[3][i]-t[clavicle].v[3][i];to[i]=goal->v[3][i]-t[clavicle].v[3][i];}
            if(normalize(from)>1.e-8f && normalize(to)>1.e-8f)
            {
                cosine=dot(from,to);if(cosine>1)cosine=1;if(cosine< -1)cosine=-1;angle=acosf(cosine);
                /* Share reach with a bounded clavicle rotation, never a
                   translation or a permanently widened shoulder. */
                fraction=(d/(length1+length2)-.95f)*2.f;if(fraction>1)fraction=1;
                if(angle*fraction>.34906585f)fraction=.34906585f/angle;
                shoulder=t[clavicle];
                for(i=0;i<3;i++)reach[i]=from[i]*(1-fraction)+to[i]*fraction;
                aim_segment(&shoulder,from,reach);halo_custom_matrix_inverse(&t[clavicle],&inverse);
                halo_custom_matrix_multiply(&shoulder,&inverse,&delta);t[clavicle]=shoulder;changed[clavicle]=1;
                for(i=0;i<p->count;i++)if(i!=clavicle)
                {at=p->parent[i];steps=0;while(at!=-1 && at!=clavicle && steps++<p->count)at=p->parent[at];if(at==clavicle)halo_custom_matrix_multiply(&delta,&t[i],&t[i]);}
            }
        }
    }
    for(j=0;j<3;j++)to[j]=goal->v[3][j]-t[upper].v[3][j];
    if(normalize(to)<1.e-8f){memcpy(t,old,p->count*sizeof(*t));return 0;}
    contact_pole(p,s,t,upper,forearm,wrist,to,pole);
    if(!arm_solve(&t[upper],&t[forearm],&t[wrist],goal,pole)){memcpy(t,old,p->count*sizeof(*t));return 0;}
    changed[upper]=changed[forearm]=changed[wrist]=1;
    /* Move every off-chain descendant with its nearest corrected ancestor.
       This includes fingers and any extra branches of a first-person rig. */
    for(i=0;i<p->count;i++)if(!changed[i])
    {
        at=p->parent[i];steps=0;while(at!=-1 && !changed[at] && steps++<p->count)at=p->parent[at];
        if(at!=-1){halo_custom_matrix_inverse(&old[at],&inverse);halo_custom_matrix_multiply(&t[at],&inverse,&delta);halo_custom_matrix_multiply(&delta,&old[i],&t[i]);}
    }
    return 1;
}
/* First-person guns retain their animation; the same constrained solver and
   descendant handling are used for the optional replacement hands. */
int halo_custom_retarget_grip(struct halo_custom_retarget const *p,struct halo_custom_matrix const *s,struct halo_custom_matrix *t,int upper,int forearm,int wrist)
{struct halo_custom_matrix goal;if(wrist<0 || wrist>=p->count)return 0;goal=t[wrist];memcpy(goal.v[3],s[wrist].v[3],12);return halo_custom_retarget_contact(p,s,t,-1,upper,forearm,wrist,&goal);}
/* Explicit little-endian representation: no ABI padding, pointers or host
   endianness. A digest, bounds and rigid-matrix checks reject corrupt caches. */
int halo_custom_retarget_encode(struct halo_custom_retarget const *plan,unsigned char const key[32],unsigned char data[HALO_RETARGET_CACHE_BYTES])
{
    unsigned char *p=data;int count=plan->count,i,j,k;unsigned char digest[32];
    if(count<1 || count>HALO_CUSTOM_MAX_NODES)return 0;
    memcpy(p,"HCERTGT3",8);p+=8;put_u32(&p,HALO_RETARGET_VERSION);memcpy(p,key,32);p+=32;
    put_u32(&p,plan->identical);put_u32(&p,(unsigned int)count);
    for(i=0;i<count;i++)
    {
        struct halo_custom_matrix const *m[4]={&plan->source_inverse[i],&plan->target[i],&plan->target_inverse[i],&plan->rotation_map[i]};
        put_u32(&p,(unsigned int)(plan->parent[i]+1));put_u32(&p,plan->map[i]);
        for(j=0;j<4;j++){put_float(&p,m[j]->scale);for(k=0;k<12;k++)put_float(&p,((float const *)m[j]->v)[k]);}
    }
    p2p_sha256(data,(int)(p-data),digest);memcpy(p,digest,32);p+=32;return (int)(p-data);
}
int halo_custom_retarget_decode(unsigned char const *data,unsigned int size,unsigned char const key[32],struct halo_custom_retarget *plan)
{
    unsigned char const *p=data;unsigned char digest[32];struct halo_custom_retarget r;struct halo_custom_matrix check;
    unsigned int parent,map;int count,i,j,k,at,steps,seen[HALO_CUSTOM_MAX_NODES]={0};
    if(size<84 || size>HALO_RETARGET_CACHE_BYTES || memcmp(p,"HCERTGT3",8))return 0;p+=8;if(get_u32(&p)!=HALO_RETARGET_VERSION || memcmp(p,key,32))return 0;p+=32;
    p2p_sha256(data,(int)size-32,digest);if(memcmp(digest,data+size-32,32))return 0;
    memset(&r,0,sizeof(r));r.identical=(int)get_u32(&p);if(r.identical!=0 && r.identical!=1)return 0;
    count=(int)get_u32(&p);if(count<1 || count>HALO_CUSTOM_MAX_NODES || size!=84u+(unsigned int)count*216u)return 0;r.count=count;
    for(i=0;i<count;i++)
    {
        struct halo_custom_matrix *m[4]={&r.source_inverse[i],&r.target[i],&r.target_inverse[i],&r.rotation_map[i]};
        parent=get_u32(&p);map=get_u32(&p);if(parent>(unsigned int)count || map>=(unsigned int)count || seen[map]++)return 0;
        r.parent[i]=(int)parent-1;r.map[i]=(int)map;
        for(j=0;j<4;j++){m[j]->scale=get_float(&p);for(k=0;k<12;k++)((float *)m[j]->v)[k]=get_float(&p);if(!matrix_valid(m[j]))return 0;}
        if(fabsf(r.rotation_map[i].scale-1.f)>1.e-6f || dot(r.rotation_map[i].v[3],r.rotation_map[i].v[3])>1.e-12f)return 0;
        halo_custom_matrix_multiply(&r.target[i],&r.target_inverse[i],&check);
        for(j=0;j<12;j++)if(fabsf(((float *)check.v)[j]-(j<9 && j%4==0 ? 1.f:0.f))>.001f)return 0;
        if(r.identical)
        {
            halo_custom_matrix_multiply(&r.target[i],&r.source_inverse[i],&check);
            for(j=0;j<12;j++)if(fabsf(((float *)check.v)[j]-(j<9 && j%4==0 ? 1.f:0.f))>.001f)return 0;
        }
    }
    for(i=0;i<count;i++)
    {at=i;steps=0;while(at!=-1 && steps++<count)at=r.parent[at];if(at!=-1 || ((i==0)!=(r.parent[i]==-1)))return 0;}
    *plan=r;return 1;
}

/* Existing body callers retain the fixed Chief-rig interface. */
int halo_custom_skeleton_hash(struct halo_custom_node const *s,struct halo_custom_node const *t,unsigned char hash[32])
{return halo_custom_skeleton_hash_count(HALO_CUSTOM_MODEL_NODES,s,t,hash);}
int halo_custom_retarget_build(struct halo_custom_node const *s,struct halo_custom_node const *t,struct halo_custom_retarget *p)
{return halo_custom_retarget_build_count(HALO_CUSTOM_MODEL_NODES,s,t,p);}
