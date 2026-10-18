#include "harness.h"
#include "halo_custom_models.h"
struct model_node {char name[32];int parent;float translation[3],rotation[4];};
struct model {struct {int count;struct model_node *address;}nodes;};
#include "skeleton.inc"
int main(int argc,char **argv){char const *case_name=argc>1?argv[1]:"";struct model_node nodes[19]={0};struct model stock={{19,nodes}};struct halo_custom_mesh mesh={0};struct halo_custom_vertex v={0};int i;
 for(i=0;i<19;i++){snprintf(nodes[i].name,32,"bone%d",i);snprintf(mesh.nodes[i].name,32,"bone%d",i);mesh.nodes[i].parent=i-1;}
 mesh.node_count=19;v.node[0]=1;v.node[1]=18;v.weight=.35f;v.position[0]=5;mesh.vertices=&v;mesh.vertex_count=1;
 CASE("rest"){for(i=0;i<19;i++){mesh.nodes[i].translation[0]=i*3;mesh.nodes[i].rotation[0]=1;mesh.nodes[i].parent=-1;}CHECK(skeleton_remap(&mesh,&stock),"different rest transforms rejected");CHECK(v.node[0]==1&&v.node[1]==18&&v.weight==.35f&&v.position[0]==5,"authored mesh changed");}
 else CASE("order"){for(i=0;i<19;i++)snprintf(mesh.nodes[i].name,32,"bone%d",18-i);CHECK(skeleton_remap(&mesh,&stock)&&v.node[0]==17&&v.node[1]==0,"weights not remapped by bone name");}
 else CASE("missing"){strcpy(mesh.nodes[4].name,"different");CHECK(!skeleton_remap(&mesh,&stock),"unknown bone accepted");}
 else CASE("duplicate"){strcpy(mesh.nodes[4].name,"bone1");CHECK(!skeleton_remap(&mesh,&stock),"duplicate bone accepted");}
 else CASE("bad-weight-index"){v.node[0]=19;CHECK(!skeleton_remap(&mesh,&stock),"invalid influence accepted");}
 else CASE("stock"){stock.nodes.count=18;CHECK(!skeleton_remap(&mesh,&stock),"incompatible target rig accepted");}
 else return 2;return 0;}
