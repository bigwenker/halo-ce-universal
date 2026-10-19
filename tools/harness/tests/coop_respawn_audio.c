#include "harness.h"
#include <stdlib.h>
#include <string.h>
static byte *respawn_samples[2];static long respawn_sample_sizes[2];static int frees;
static void checked_free(void *p){CHECK(p,"game allocator rejects free(NULL)");free(p);frees++;}
#define free checked_free
#include "under_test.inc"
#undef free
static byte map[8192];static byte *tags=map+2048;
static void set(byte *p,unsigned long value){memcpy(p,&value,4);}
static void setup(void){memset(map,0,sizeof(map));set(map+16,2048);set(map+20,1024);
 set(tags,0x803a6024UL);set(tags+12,1);set(tags+36,'snd!');set(tags+52,0x803a6058UL);set(tags+56,0x803a6080UL);
 strcpy((char*)tags+88,respawn_sound_names[0]);set(tags+280,1);set(tags+284,0x803a6180UL);
 set(tags+444,1);set(tags+448,0x803a6200UL);tags[552]=1;tags[554]=tags[555]=255;
 set(tags+576,36);set(tags+584,4096);memset(map+4096,123,36);}
int main(int argc,char **argv){char const *case_name=argc>1?argv[1]:"";setup();
 CASE("samples") {CHECK(audio_samples(map,sizeof(map),0),"valid sample rejected");CHECK(respawn_sample_sizes[0]==36&&!memcmp(respawn_samples[0],map+4096,36),"wrong samples");audio_release(respawn_samples[0]);}
 else CASE("bounds") {
  set(tags+12,32768);CHECK(!audio_samples(map,sizeof(map),0),"oversized table");setup();set(tags+52,0xffffffffUL);CHECK(!audio_samples(map,sizeof(map),0),"bad name pointer");
  setup();set(tags+284,0xffffffffUL);CHECK(!audio_samples(map,sizeof(map),0),"bad range pointer");setup();set(tags+576,1048577);CHECK(!audio_samples(map,sizeof(map),0),"oversized sample");
  setup();set(tags+584,0xffffffffUL);CHECK(!audio_samples(map,sizeof(map),0),"sample range overflow");setup();tags[552]=3;CHECK(!audio_samples(map,sizeof(map),0),"unsupported codec");
 } else CASE("cleanup") {audio_release(NULL);CHECK(!frees,"null free");}else return 2;return 0;}
