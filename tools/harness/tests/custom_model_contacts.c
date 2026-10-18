#include "harness.h"
#include <math.h>
#include "halo_custom_retarget.h"
#include "sha.inc"
#include "under_test.inc"

static float distance(float const *a,float const *b)
{float v[3];int i;for(i=0;i<3;i++)v[i]=a[i]-b[i];return sqrtf(dot(v,v));}
static void close_value(float a,float b) {CHECK(isfinite(a) && fabsf(a-b)<.00015f,"%g != %g",a,b);}
static void bind(struct halo_custom_node const *n,int count,struct halo_custom_matrix *m)
{int i;struct halo_custom_matrix local;for(i=0;i<count;i++){halo_custom_matrix_node(&n[i],&local);if(n[i].parent>=0)halo_custom_matrix_multiply(&m[n[i].parent],&local,&m[i]);else m[i]=local;}}
static void fixture(struct halo_custom_node *n)
{
    int i;memset(n,0,19*sizeof(*n));
    for(i=0;i<19;i++){snprintf(n[i].name,32,"bone%d",i);n[i].parent=i ? 0:-1;n[i].rotation[3]=1;}
    strcpy(n[1].name,"clavicle");n[1].translation[1]=.12f;n[1].translation[2]=.5f;
    n[2].parent=1;n[2].translation[1]=.04f;
    n[3].parent=2;n[3].translation[1]=.025f;n[3].translation[2]=-.11f;
    n[4].parent=3;n[4].translation[0]=.016f;n[4].translation[1]=.025f;n[4].translation[2]=-.11f;
    n[5].parent=4;n[5].translation[0]=.025f;n[6].parent=5;n[6].translation[0]=.015f;
    n[7].parent=3;n[7].translation[1]=.01f;
    for(i=8;i<19;i++){n[i].parent=i-1;n[i].translation[0]=.01f;}
}
int main(int argc,char **argv)
{
    char const *name=argc>1 ? argv[1]:"";
    struct halo_custom_node s[19],t[19],animated[19];struct halo_custom_retarget p,loaded;
    struct halo_custom_matrix source[19],target[19],other[19],old[19],goal,inverse,relative,after;
    unsigned char key[32],bytes[HALO_RETARGET_CACHE_BYTES];float from[3],orth[3],reach,along,elbows[2][3],len1,len2;int i,j,k,size;
    fixture(s);memcpy(t,s,sizeof(t));t[1].translation[1]=.08f;t[2].translation[1]=.03f;t[3].translation[1]=.03f;t[4].translation[0]=0;t[4].translation[1]=.032f;
    CHECK(halo_custom_retarget_build(s,t,&p),"plan");bind(s,19,source);CHECK(halo_custom_retarget_pose(&p,source,target),"pose");memcpy(old,target,sizeof(old));
    if(!strcmp(name,"axes"))
    {
        /* Same exported joint orientations, but the target's forearm
           leaves the elbow along Y instead of X. A source Y-axis bend
           must still flex the target segment into Z, not twist it in place. */
        memset(s,0,sizeof(s));for(i=0;i<19;i++){snprintf(s[i].name,32,"bone%d",i);s[i].parent=i ? i-1:-1;s[i].rotation[3]=1;s[i].translation[0]=i ? 1:0;}
        memcpy(t,s,sizeof(t));t[2].translation[0]=0;t[2].translation[1]=1;
        CHECK(halo_custom_retarget_build(s,t,&p),"axis plan");s[1].rotation[1]=sinf(.785398163f);s[1].rotation[3]=cosf(.785398163f);
        bind(s,19,source);CHECK(halo_custom_retarget_pose(&p,source,target),"axis pose");
        close_value(target[2].v[3][0]-target[1].v[3][0],0);close_value(target[2].v[3][1]-target[1].v[3][1],0);close_value(target[2].v[3][2]-target[1].v[3][2],1);
    }
    else if(!strcmp(name,"pole"))
    {
        for(i=0;i<3;i++)from[i]=target[3].v[3][i]-target[2].v[3][i];len1=normalize(from);
        reach=.75f*(len1+distance(target[4].v[3],target[3].v[3]));orth[0]=0;orth[1]=1;orth[2]=0;
        along=dot(orth,from);for(i=0;i<3;i++)orth[i]-=along*from[i];normalize(orth);
        for(k=0;k<2;k++)
        {
            memcpy(target,old,sizeof(old));goal=target[4];for(i=0;i<3;i++)goal.v[3][i]=target[2].v[3][i]+reach*(from[i]+(k ? 1:-1)*1.e-4f*orth[i]);
            CHECK(halo_custom_retarget_arm(&target[2],&target[3],&target[4],&goal),"pole solve");memcpy(elbows[k],target[3].v[3],12);
        }
        CHECK(distance(elbows[0],elbows[1])<.0001f,"elbow flipped across the arm: %g",distance(elbows[0],elbows[1]));
    }
    else if(!strcmp(name,"cache-version"))
    {
        CHECK(halo_custom_skeleton_hash(s,t,key),"key");size=halo_custom_retarget_encode(&p,key,bytes);
        CHECK(halo_custom_retarget_decode(bytes,size,key,&loaded),"new cache rejected");bytes[7]='2';CHECK(!halo_custom_retarget_decode(bytes,size,key,&loaded),"old algorithm cache accepted");
    }
    else if(!strcmp(name,"source-pole"))
    {
        for(i=0;i<3;i++)from[i]=source[4].v[3][i]-source[2].v[3][i];reach=normalize(from);
        orth[0]=1;orth[1]=orth[2]=0;along=dot(orth,from);for(i=0;i<3;i++)orth[i]-=along*from[i];normalize(orth);
        memcpy(other,source,sizeof(source));goal=old[4];goal.v[3][2]+=.03f;
        for(k=0;k<2;k++)
        {
            memcpy(source,other,sizeof(other));memcpy(target,old,sizeof(old));
            for(i=0;i<3;i++)source[3].v[3][i]=source[2].v[3][i]+from[i]*reach*.5f+(k ? 1:-1)*1.e-7f*orth[i];
            CHECK(halo_custom_retarget_contact(&p,source,target,-1,2,3,4,&goal),"straight source solve");memcpy(elbows[k],target[3].v[3],12);
        }
        CHECK(distance(elbows[0],elbows[1])<.0001f,"near-straight source flipped the elbow");
    }
    else if(!strcmp(name,"invalid"))
    {
        goal=target[4];goal.v[0][0]=NAN;
        CHECK(!halo_custom_retarget_contact(&p,source,target,1,2,3,4,&goal),"invalid contact accepted");CHECK(!memcmp(old,target,sizeof(old)),"failed contact altered the rig");
        CHECK(!halo_custom_retarget_contact(&p,source,target,0,2,3,4,&old[4]),"wrong clavicle accepted");
    }
    else
    {
        goal=target[4];goal.v[3][0]+=.06f;goal.v[3][2]+=.04f;
        if(!strcmp(name,"shoulder")){goal.v[3][0]=.22f;goal.v[3][1]=.16f;goal.v[3][2]=.47f;}
        if(!strcmp(name,"limits")){goal.v[3][0]=100;goal.v[3][1]=-100;}
        if(!strcmp(name,"scale"))
        {
            struct halo_custom_matrix world;identity_matrix(&world);world.scale=2;world.v[3][0]=100;world.v[3][1]=-80;
            for(i=0;i<19;i++){halo_custom_matrix_multiply(&world,&source[i],&source[i]);halo_custom_matrix_multiply(&world,&target[i],&target[i]);}
            halo_custom_matrix_multiply(&world,&goal,&goal);memcpy(old,target,sizeof(old));
        }
        len1=distance(target[2].v[3],target[3].v[3]);len2=distance(target[3].v[3],target[4].v[3]);
        CHECK(halo_custom_retarget_contact(&p,source,target,1,2,3,4,&goal),"contact solve");
        close_value(distance(target[2].v[3],target[3].v[3]),len1);close_value(distance(target[3].v[3],target[4].v[3]),len2);
        close_value(distance(target[1].v[3],target[2].v[3]),distance(old[1].v[3],old[2].v[3]));
        for(i=0;i<19;i++)CHECK(matrix_valid(&target[i]),"invalid output");
        if(!strcmp(name,"limits"))
        {
            for(i=0;i<3;i++){from[i]=target[3].v[3][i]-target[2].v[3][i];orth[i]=target[4].v[3][i]-target[3].v[3][i];}normalize(from);normalize(orth);
            CHECK(dot(from,orth)>=cosf(2.79253f)-.0001f && dot(from,orth)<=cosf(.0349066f)+.0001f,"elbow exceeded flexion limits");
        }
        if(!strcmp(name,"shoulder"))
        {
            CHECK(distance(target[2].v[3],old[2].v[3])>1.e-5f,"clavicle did not share reach");
            for(i=0;i<3;i++){from[i]=old[2].v[3][i]-old[1].v[3][i];orth[i]=target[2].v[3][i]-target[1].v[3][i];}normalize(from);normalize(orth);
            CHECK(dot(from,orth)>=cosf(.3491f),"clavicle exceeded 20 degrees");
        }
        if(!strcmp(name,"descendants"))for(k=5;k<19;k++)
        {
            j=k==7 ? 3:k<7 ? 4:7;
            halo_custom_matrix_inverse(&old[j],&inverse);halo_custom_matrix_multiply(&inverse,&old[k],&relative);
            halo_custom_matrix_inverse(&target[j],&inverse);halo_custom_matrix_multiply(&inverse,&target[k],&after);
            for(i=0;i<12;i++)close_value(((float *)relative.v)[i],((float *)after.v)[i]);
        }
        if(!strcmp(name,"frames"))for(k=0;k<1000;k++)
        {
            memcpy(animated,s,sizeof(s));animated[2].rotation[0]=sinf(k*.001f);animated[2].rotation[3]=cosf(k*.001f);
            animated[3].rotation[1]=sinf(k*.002f);animated[3].rotation[3]=cosf(k*.002f);bind(animated,19,source);
            CHECK(halo_custom_retarget_pose(&p,source,target),"animated pose");goal=target[4];goal.v[3][0]+=.015f;
            CHECK(halo_custom_retarget_contact(&p,source,target,1,2,3,4,&goal),"animated contact");
            if(k)CHECK(distance(target[3].v[3],other[3].v[3])<.01f,"animated elbow discontinuity at %d",k);
            memcpy(other,target,sizeof(other));
        }
    }
    return 0;
}
