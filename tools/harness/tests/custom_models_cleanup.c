#include "harness.h"
#define HALO_CUSTOM_MATERIALS 32
struct bitmap_data {int v;};
struct buffer {void *hardware_format;};
struct custom_part {struct buffer vertex_buffer,triangle_buffer;};
struct custom_asset {struct custom_part parts[HALO_CUSTOM_MATERIALS];void *vertex_data[HALO_CUSTOM_MATERIALS],*index_data[HALO_CUSTOM_MATERIALS];struct bitmap_data *bitmaps[HALO_CUSTOM_MATERIALS][2];unsigned long memory_bytes;};
static unsigned long geometry_bytes;
static int freed,vertices,triangles,bitmaps;
static void test_free(void *p){CHECK(p,"game debug_free rejects NULL");freed++;free(p);}
static void rasterizer_vertex_buffer_delete(struct buffer *p){vertices++;p->hardware_format=NULL;}
static void rasterizer_triangle_buffer_delete(struct buffer *p){triangles++;p->hardware_format=NULL;}
static void bitmap_delete(struct bitmap_data *p){bitmaps++;test_free(p);}
#define free test_free
#include "cleanup.inc"
int main(int argc,char **argv){struct custom_asset asset={0};char const *case_name=argc>1?argv[1]:"";
 CASE("empty"){asset_dispose(&asset);CHECK(!freed&&!vertices&&!triangles&&!bitmaps,"empty model disposal touched memory");}
 else CASE("partial"){
  geometry_bytes=120;asset.memory_bytes=100;
  asset.vertex_data[31]=malloc(4);asset.parts[31].vertex_buffer.hardware_format=(void*)1;
  asset.bitmaps[31][1]=malloc(sizeof(struct bitmap_data));
  asset_dispose(&asset);CHECK(freed==2&&vertices==1&&!triangles&&bitmaps==1&&geometry_bytes==20,"partial model cleanup");
  asset_dispose(&asset);CHECK(freed==2&&geometry_bytes==20,"double release");
 }else return 2;return 0;}
