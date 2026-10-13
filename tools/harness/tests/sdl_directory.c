#include "harness.h"
#include <stdbool.h>
#define SDLCALL
typedef enum {SDL_ENUM_CONTINUE,SDL_ENUM_SUCCESS,SDL_ENUM_FAILURE} SDL_EnumerationResult;
typedef SDL_EnumerationResult (*SDL_EnumerateDirectoryCallback)(void *,const char *,const char *);
static int in_native, calls, count=2, mode, malformed;
static bool SDL_SetError(const char *text) {(void)text;return false;}
static bool native_enumerate(const char *path,SDL_EnumerateDirectoryCallback callback,void *context) {
    CHECK(!strcmp(path,"/tmp/sprays"),"wrong native path");in_native=1;
    if(mode==3){in_native=0;return false;}
    for(int i=0;i<count;i++) if(callback(context,path,i?"spray_b.gif":"spray_a.jpg")!=SDL_ENUM_CONTINUE) {in_native=0;return false;}
    in_native=0;return true;
}
/* Fault injection models an invalid import response, not a directory's contents. */
static int import_list(const char *,char *,unsigned int);
#include "under_test.inc"
static int import_list(const char *path,char *names,unsigned int capacity) {
    if(malformed){memcpy(names,"bad",3);return 3;}
    return host_sdl_directory_list(path,names,capacity);
}
static SDL_EnumerationResult consumer(void *userdata,const char *directory,const char *name) {
    CHECK(!in_native,"guest callback invoked inside native enumeration");
    CHECK(userdata==&calls,"guest context changed");CHECK(!strcmp(directory,"/tmp/sprays/"),"directory lacks separator");
    CHECK(!strcmp(name,calls?"spray_b.gif":"spray_a.jpg"),"name changed");calls++;
    return mode==1?SDL_ENUM_SUCCESS:mode==2?SDL_ENUM_FAILURE:SDL_ENUM_CONTINUE;
}
int main(int argc,char **argv) {
    char const *case_name=argc>1?argv[1]:"";
    if(!strcmp(case_name,"stop")) mode=1;
    if(!strcmp(case_name,"failure")) mode=2;
    if(!strcmp(case_name,"host-error")) mode=3;
    if(!strcmp(case_name,"empty")) count=0;
    if(!strcmp(case_name,"malformed")) malformed=1;
    if(!strcmp(case_name,"overflow")) {
        char buffer[4]={1,2,3,4};CHECK(host_sdl_directory_list("/tmp/sprays",buffer,2)==-1,"overflow accepted");
        CHECK(buffer[2]==3&&buffer[3]==4,"native listing wrote beyond capacity");return 0;
    }
    if(!strcmp(case_name,"path-limit")) {
        char path[4098];memset(path,'a',sizeof(path)-1);path[sizeof(path)-1]=0;
        CHECK(!SDL_EnumerateDirectory(path,consumer,&calls)&&!calls,"overlong path accepted");return 0;
    }
    CHECK(SDL_EnumerateDirectory("/tmp/sprays",consumer,&calls)==(!malformed&&mode!=2&&mode!=3),"wrong result");
    CHECK(calls==(malformed||mode==3?0:mode==1||mode==2?1:count),"wrong callback count");return 0;
}
