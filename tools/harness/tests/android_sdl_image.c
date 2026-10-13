#include "harness.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
static __thread const char *host_base = "/game/";
static pthread_barrier_t path_barrier;
typedef int64_t Sint64;
typedef int64_t SDL_Time;
typedef int SDL_PixelFormat;
typedef int SDL_PathType;
typedef void SDL_IOStream;
typedef enum {SDL_ENUM_CONTINUE,SDL_ENUM_SUCCESS,SDL_ENUM_FAILURE} SDL_EnumerationResult;
typedef SDL_EnumerationResult (*SDL_EnumerateDirectoryCallback)(void *,const char *,const char *);
typedef struct {int year,month,day,hour,minute,second,nanosecond,day_of_week,utc_offset;} SDL_DateTime;
typedef struct {SDL_PathType type;uint64_t size;SDL_Time create_time,modify_time,access_time;} SDL_PathInfo;
typedef struct {int w,h,pitch,format,refcount;void *pixels;} SDL_Surface;
#define SDL_PIXELFORMAT_RGBA32 1
static int saved,save_result=1,exists=1;
static const void *saved_pixels;
static char error_text[256];
static void host_sdl_set_error(const char *message){snprintf(error_text,sizeof(error_text),"%s",message);}
static int host_sdl_base_path(char *buffer,unsigned size){snprintf(buffer,size,"%s",host_base);return 1;}
static int host_sdl_create_directory(const char *path){return !strcmp(path,"/game/screenshots");}
static int host_sdl_directory_list(const char *path,char *names,unsigned capacity){(void)path;(void)names;(void)capacity;return -1;}
static int host_sdl_current_time(void *ticks){*(int64_t *)ticks=1234567890123LL;return 1;}
static int host_sdl_date_time(long long ticks,void *date,int local){CHECK(ticks==1234567890123LL&&local==1,"time truncated");((SDL_DateTime *)date)->year=2026;return 1;}
static int host_sdl_path_info(const char *path,int *type,long long *values){CHECK(!strcmp(path,"spray.png"),"wrong path");if(!exists)return 0;if(type)*type=2;if(values){values[0]=9876543210LL;values[1]=11;values[2]=22;values[3]=33;}return 1;}
static int host_sdl_save_rgba_png(int w,int h,int pitch,const void *pixels,const char *path){CHECK(w==3&&h==2&&pitch==12&&!strcmp(path,"test.png"),"wrong screenshot descriptor");CHECK(((const unsigned char *)pixels)[23]==42,"pixel buffer not shared");saved++;saved_pixels=pixels;return save_result;}
#include "under_test.inc"
static void *path_worker(void *unused){
 (void)unused;host_base="/worker/";const char *path=SDL_GetBasePath();
 pthread_barrier_wait(&path_barrier);pthread_barrier_wait(&path_barrier);
 CHECK(!strcmp(path,"/worker/"),"worker base path changed");return NULL;
}
int main(int argc,char **argv){
 const char *case_name=argc>1?argv[1]:"";
 CASE("thread-path"){
  const char *path=SDL_GetBasePath();pthread_t worker;
  CHECK(!pthread_barrier_init(&path_barrier,NULL,2),"barrier failed");CHECK(!pthread_create(&worker,NULL,path_worker,NULL),"worker failed");
  pthread_barrier_wait(&path_barrier);CHECK(!strcmp(path,"/game/"),"worker overwrote game base path");pthread_barrier_wait(&path_barrier);
  CHECK(!pthread_join(worker,NULL),"join failed");pthread_barrier_destroy(&path_barrier);return 0;
 }
 CASE("file") {
  char name[]="/tmp/halo-spray-XXXXXX";int fd=mkstemp(name);CHECK(fd>=0,"temp file");CHECK(write(fd,"PNGdata",7)==7,"write");close(fd);
  SDL_IOStream *stream=SDL_IOFromFile(name,"rb");CHECK(stream,"open");int opened=fileno((FILE *)stream);
  CHECK(SDL_GetIOSize(stream)==7&&ftell((FILE *)stream)==0,"size moved position");size_t n=999;char *bytes=SDL_LoadFile_IO(stream,&n,true);
  CHECK(n==7&&bytes&&!memcmp(bytes,"PNGdata",7)&&bytes[7]==0,"file bytes");CHECK(fcntl(opened,F_GETFD)==-1,"stream not closed");free(bytes);unlink(name);return 0;
 }
 CASE("empty"){FILE *file=tmpfile();size_t n=123;char *data=SDL_LoadFile_IO((SDL_IOStream *)file,&n,false);CHECK(data&&n==0&&data[0]==0,"empty file");free(data);CHECK(SDL_CloseIO((SDL_IOStream *)file),"close");return 0;}
 CASE("failure"){CHECK(!SDL_IOFromFile("/nonexistent/halo-spray.png","rb"),"missing file admitted");return 0;}
 CASE("format"){CHECK(!SDL_CreateSurface(2,2,99),"unsupported format admitted");return 0;}
 CASE("overflow"){CHECK(!SDL_CreateSurface(INT_MAX,2,1)&&!SDL_CreateSurface(0,2,1)&&!SDL_CreateSurface(2,-1,1)&&!SDL_CreateSurface(INT_MAX/4,INT_MAX,1),"invalid size accepted");return 0;}
 CASE("surface"){}
 CASE("save-failure") save_result=0;
 if(!strcmp(case_name,"surface")||!strcmp(case_name,"save-failure")){
  SDL_Surface *surface=SDL_CreateSurface(3,2,1);CHECK(surface&&surface->pitch==12&&surface->w==3&&surface->h==2&&surface->format==1,"surface metadata");
  ((unsigned char *)surface->pixels)[23]=42;CHECK(SDL_SavePNG(surface,"test.png")==!!save_result&&saved==1&&saved_pixels==surface->pixels,"save bridge");SDL_DestroySurface(surface);SDL_DestroySurface(NULL);return 0;
 }
 CASE("path"){SDL_PathInfo info={0};CHECK(SDL_GetPathInfo("spray.png",NULL)&&SDL_GetPathInfo("spray.png",&info),"stat failed");CHECK(info.type==2&&info.size==9876543210ULL&&info.create_time==11&&info.modify_time==22&&info.access_time==33,"metadata marshaling");exists=0;CHECK(!SDL_GetPathInfo("spray.png",NULL),"missing path succeeds");return 0;}
 CASE("time"){SDL_Time ticks;SDL_DateTime date={0};CHECK(SDL_GetCurrentTime(&ticks)&&SDL_TimeToDateTime(ticks,&date,true)&&date.year==2026,"time bridge");CHECK(SDL_CreateDirectory("/game/screenshots")&&!strcmp(SDL_GetBasePath(),"/game/"),"path bridge");return 0;}
 CASE("error"){char buffer[8];CHECK(!SDL_SetError("error %s %d","100%",17)&&!strcmp(error_text,"error 100% 17"),"variadic error");CHECK(SDL_snprintf(buffer,sizeof(buffer),"%s%d","abc",123456)==9&&!strcmp(buffer,"abc1234"),"snprintf truncation");return 0;}
 CHECK(0,"unknown case");return 0;
}
