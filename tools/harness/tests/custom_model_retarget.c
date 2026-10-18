#include "harness.h"
#include <math.h>
#include "halo_custom_retarget.h"
#include "sha.inc"
#include "under_test.inc"

static void fixture(struct halo_custom_node n[19])
{
    int i;memset(n,0,19*sizeof(*n));
    for(i=0;i<19;i++){snprintf(n[i].name,32,"bone%02d",i);n[i].parent=i-1;n[i].rotation[3]=1;n[i].translation[0]=i ? 1:.5f;}
}
static void bind(struct halo_custom_node const n[19],struct halo_custom_matrix m[19])
{int i;struct halo_custom_matrix local;for(i=0;i<19;i++){halo_custom_matrix_node(&n[i],&local);if(i)halo_custom_matrix_multiply(&m[i-1],&local,&m[i]);else m[i]=local;}}
static void close_value(float a,float b) {CHECK(isfinite(a) && fabsf(a-b)<.00015f,"%g != %g",a,b);}
static void close_matrix(struct halo_custom_matrix const *a,struct halo_custom_matrix const *b)
{int i;close_value(a->scale,b->scale);for(i=0;i<12;i++)close_value(((float const *)a->v)[i],((float const *)b->v)[i]);}
static float distance(float const *a,float const *b) {float v[3];int i;for(i=0;i<3;i++)v[i]=a[i]-b[i];return sqrtf(dot(v,v));}
int main(int argc,char **argv)
{
    char const *case_name=argc>1 ? argv[1]:"";struct halo_custom_node s[19],t[19],animated[19];struct halo_custom_retarget plan,loaded;
    struct halo_custom_matrix source[19],target[19],expected[19],skin,identity,goal;
    unsigned char key[32],other[32],bytes[HALO_RETARGET_CACHE_BYTES];int i,j,size;
    fixture(s);memcpy(t,s,sizeof(t));t[1].translation[0]=.7f;t[2].translation[0]=1.3f;
    CHECK(halo_custom_skeleton_hash(s,t,key),"hash failed");CHECK(halo_custom_retarget_build(s,t,&plan),"plan failed");
    bind(s,source);bind(t,expected);
    if(!strcmp(case_name,"identity"))
    {
        CHECK(halo_custom_retarget_build(s,s,&plan)&&plan.identical,"identity not detected");
        s[2].rotation[2]=sinf(.4f);s[2].rotation[3]=cosf(.4f);s[0].translation[1]=4;bind(s,source);
        CHECK(halo_custom_retarget_pose(&plan,source,target),"pose failed");CHECK(!memcmp(source,target,sizeof(source)),"stock changed");
    }
    else if(!strcmp(case_name,"rest") || !strcmp(case_name,"axes"))
    {
        if(!strcmp(case_name,"axes")){t[1].rotation[2]=sinf(.8f);t[1].rotation[3]=cosf(.8f);CHECK(halo_custom_retarget_build(s,t,&plan),"axis plan");bind(t,expected);}
        CHECK(halo_custom_retarget_pose(&plan,source,target),"rest failed");
        memset(&identity,0,sizeof(identity));identity.scale=1;for(i=0;i<3;i++)identity.v[i][i]=1;
        for(i=0;i<19;i++){close_matrix(&target[i],&expected[i]);halo_custom_matrix_multiply(&target[i],&plan.target_inverse[i],&skin);close_matrix(&skin,&identity);}
    }
    else if(!strcmp(case_name,"world"))
    {
        struct halo_custom_node frame;struct halo_custom_matrix world;
        memset(&frame,0,sizeof(frame));frame.rotation[1]=sinf(.6f);frame.rotation[3]=cosf(.6f);frame.translation[0]=100;frame.translation[1]=-50;
        halo_custom_matrix_node(&frame,&world);world.scale=2;
        for(i=0;i<19;i++){halo_custom_matrix_multiply(&world,&source[i],&source[i]);halo_custom_matrix_multiply(&world,&expected[i],&expected[i]);}
        CHECK(halo_custom_retarget_pose(&plan,source,target),"world-space pose failed");for(i=0;i<19;i++)close_matrix(&target[i],&expected[i]);
    }
    else if(!strcmp(case_name,"motion") || !strcmp(case_name,"translation") || !strcmp(case_name,"axes-motion"))
    {
        if(!strcmp(case_name,"axes-motion")){t[1].rotation[2]=sinf(.8f);t[1].rotation[3]=cosf(.8f);CHECK(halo_custom_retarget_build(s,t,&plan),"axis plan");}
        memcpy(animated,s,sizeof(s));animated[0].rotation[1]=sinf(.35f);animated[0].rotation[3]=cosf(.35f);
        animated[1].rotation[2]=sinf(.4f);animated[1].rotation[3]=cosf(.4f);animated[2].rotation[1]=sinf(.2f);animated[2].rotation[3]=cosf(.2f);
        if(!strcmp(case_name,"translation"))animated[1].translation[0]+=.2f;
        bind(animated,source);CHECK(halo_custom_retarget_pose(&plan,source,target),"motion failed");
        for(i=1;i<19;i++)close_value(distance(target[i].v[3],target[i-1].v[3]),t[i].translation[0]+(i==1&&!strcmp(case_name,"translation") ? .2f:0));
        if(!strcmp(case_name,"axes-motion"))for(i=0;i<19;i++)
        {
            struct halo_custom_matrix delta,want;
            halo_custom_matrix_multiply(&source[i],&plan.source_inverse[i],&delta);halo_custom_matrix_multiply(&delta,&plan.target[i],&want);
            for(j=0;j<9;j++)close_value(((float *)target[i].v)[j],((float *)want.v)[j]);
        }
        CHECK(distance(source[1].v[3],target[1].v[3])>.25f,"old Chief proportions retained");
    }
    else if(!strcmp(case_name,"benchmark"))
    {
        clock_t start=clock();
        for(i=0;i<200000;i++){CHECK(halo_custom_retarget_pose(&plan,source,target),"benchmark failed");goal=source[2];halo_custom_retarget_arm(&target[0],&target[1],&target[2],&goal);}
        printf("200000 poses and arm constraints: %.3f us/pose\n",(double)(clock()-start)*1000000.0/CLOCKS_PER_SEC/200000.0);
    }
    else if(!strcmp(case_name,"hash"))
    {
        CHECK(halo_custom_skeleton_hash(s,t,other)&&!memcmp(key,other,32),"unstable key");
        t[3].translation[1]=.001f;CHECK(halo_custom_skeleton_hash(s,t,other)&&memcmp(key,other,32),"bind edit missed");t[3].translation[1]=-0.f;
        for(i=0;i<4;i++)t[3].rotation[i]=-t[3].rotation[i];CHECK(halo_custom_skeleton_hash(s,t,other)&&!memcmp(key,other,32),"quaternion sign/negative zero changed key");
        s[3].translation[1]=.001f;CHECK(halo_custom_skeleton_hash(s,t,other)&&memcmp(key,other,32),"upstream rig edit missed");
    }
    else if(!strcmp(case_name,"invalid"))
    {
        t[2].parent=3;CHECK(!halo_custom_retarget_build(s,t,&loaded),"cycle accepted");t[2].parent=1;
        t[2].rotation[0]=NAN;CHECK(!halo_custom_skeleton_hash(s,t,other),"NaN hashed");t[2].rotation[0]=0;
        strcpy(t[2].name,t[1].name);CHECK(!halo_custom_retarget_build(s,t,&loaded),"duplicate name accepted");
    }
    else if(!strcmp(case_name,"cache") || !strcmp(case_name,"corrupt"))
    {
        size=halo_custom_retarget_encode(&plan,key,bytes);CHECK(size<=HALO_RETARGET_CACHE_BYTES,"overflow");
        CHECK(halo_custom_retarget_decode(bytes,size,key,&loaded),"roundtrip failed");
        CHECK(plan.identical==loaded.identical,"identity flag changed");
        for(i=0;i<19;i++){CHECK(plan.parent[i]==loaded.parent[i] && plan.map[i]==loaded.map[i],"mapping changed");close_matrix(&plan.source_inverse[i],&loaded.source_inverse[i]);close_matrix(&plan.target[i],&loaded.target[i]);close_matrix(&plan.target_inverse[i],&loaded.target_inverse[i]);}
        if(!strcmp(case_name,"corrupt"))
        {
            for(i=0;i<size;i+=13){bytes[i]^=1;CHECK(!halo_custom_retarget_decode(bytes,size,key,&loaded),"corrupt cache accepted");bytes[i]^=1;}
            for(i=0;i<size;i++)CHECK(!halo_custom_retarget_decode(bytes,i,key,&loaded),"truncation accepted");
            memcpy(other,key,32);other[0]^=1;CHECK(!halo_custom_retarget_decode(bytes,size,other,&loaded),"stale key accepted");
            plan.parent[2]=3;size=halo_custom_retarget_encode(&plan,key,bytes);CHECK(!halo_custom_retarget_decode(bytes,size,key,&loaded),"cyclic cached rig accepted");
        }
    }
    else
    {
        memcpy(target,expected,sizeof(target));goal=target[2];goal.v[3][0]=1.5f;goal.v[3][1]=.5f;
        if(!strcmp(case_name,"unreachable"))goal.v[3][0]=100;
        if(!strcmp(case_name,"fold")){goal.v[3][0]=target[0].v[3][0]+.01f;goal.v[3][1]=0;}
        if(!strcmp(case_name,"zero"))memcpy(goal.v[3],target[0].v[3],12);
        if(!strcmp(case_name,"straight")){goal.v[3][0]=2;goal.v[3][1]=0;}
        if(!strcmp(case_name,"zero")){CHECK(!halo_custom_retarget_arm(&target[0],&target[1],&target[2],&goal),"zero goal changed rig");return 0;}
        CHECK(halo_custom_retarget_arm(&target[0],&target[1],&target[2],&goal),"IK failed");
        close_value(distance(target[0].v[3],target[1].v[3]),.7f);close_value(distance(target[1].v[3],target[2].v[3]),1.3f);
        for(i=0;i<3;i++)CHECK(matrix_valid(&target[i]),"invalid IK matrix");
        if(!strcmp(case_name,"ik"))for(j=0;j<3;j++)close_value(target[2].v[3][j],goal.v[3][j]);
    }
    return 0;
}
