"""Real spray PNG loader and GL pass: pixels, occlusion and restored state."""
import argparse
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image
from test_screenshot import PRELUDE, block

ROOT = Path(__file__).resolve().parents[1]
PREFIX = PRELUDE.split('struct render_target_entry')[0] + r'''
#include "../include/halo_spray.h"
static int invalidations;
static void xgpu_gl_state_invalidate(void){invalidations++;}
static void console_printf(BOOL warning,const char *format,...){(void)warning;(void)format;}
static GLuint xgpu_compile_shader(GLenum type,const char *code,const char *what){
    GLuint s=glCreateShader(type);GLint ok;(void)what;
    glShaderSource(s,1,&code,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&ok);CHECK(ok);return s;
}
static GLuint xgpu_link_program(GLuint v,GLuint f,const char *what){
    GLuint p=glCreateProgram();GLint ok;(void)what;
    glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&ok);CHECK(ok);return p;
}
'''
ASYNC_HOOKS = r'''
static SDL_AtomicInt test_decode_count, test_decode_hold, test_decode_entered, test_decode_active;
static SDL_ThreadID test_render_thread;
static size_t test_uploaded;
static void spray_test_before_decode(void){
 CHECK(SDL_GetCurrentThreadID()!=test_render_thread);
 SDL_AddAtomicInt(&test_decode_count,1);SDL_AddAtomicInt(&test_decode_active,1);SDL_SetAtomicInt(&test_decode_entered,1);
 while(SDL_GetAtomicInt(&test_decode_hold))SDL_Delay(1);
}
static void GLAPIENTRY spray_test_sub_image(GLenum target,GLint level,GLint x,GLint y,GLsizei width,GLsizei height,GLenum format,GLenum type,const void *pixels){
 CHECK(SDL_GetCurrentThreadID()==test_render_thread);test_uploaded+=(size_t)width*height*4;
 glTexSubImage2D(target,level,x,y,width,height,format,type,pixels);
}
'''
ASYNC_TEST = r'''
static void gif_texture(int slot,const char *root,int frame);
static struct halo_spray_clip_vertex quad[6];
static double worst_update;
static int upload_updates;
static void async_update(void){
 Uint64 before=SDL_GetTicksNS();test_uploaded=0;spray_images_update();
 double elapsed=(SDL_GetTicksNS()-before)/1e6;if(elapsed>worst_update)worst_update=elapsed;
 CHECK(test_uploaded<=SPRAY_UPLOAD_BUDGET);if(test_uploaded)upload_updates++;
}
static void wait_visible(int slot){
 Uint64 until=SDL_GetTicks()+10000;
 while(!spray_visible[slot]&&SDL_GetTicks()<until){async_update();SDL_Delay(1);}
 CHECK(spray_visible[slot]);
}
static void wait_entered(void){
 Uint64 until=SDL_GetTicks()+2000;
 while(!SDL_GetAtomicInt(&test_decode_entered)&&SDL_GetTicks()<until)SDL_Delay(1);
 CHECK(SDL_GetAtomicInt(&test_decode_entered));
}
static void run_async(const char *root){
 char path[4096];size_t size;unsigned char hash[32];float aspect;
 test_render_thread=SDL_GetCurrentThreadID();
 SDL_snprintf(path,sizeof(path),"%s/animated.gif",root);void *bytes=SDL_LoadFile(path,&size);CHECK(bytes);crypto_blake2b(hash,32,bytes,size);
 CHECK(halo_spray_image_aspect(bytes,size,&aspect)&&aspect==1);CHECK(!SDL_GetAtomicInt(&test_decode_count));
 SDL_SetAtomicInt(&test_decode_hold,1);
 Uint64 began=SDL_GetTicksNS();CHECK(halo_spray_image_enqueue(0,bytes,size,hash,"",0));
 double enqueue_ms=(SDL_GetTicksNS()-began)/1e6;
 wait_entered();
 for(int i=0;i<100;i++){async_update();CHECK(!spray_visible[0]);halo_spray_image_draw_slot(0,quad,6);}
 CHECK(SDL_GetAtomicInt(&test_decode_active)==1);SDL_SetAtomicInt(&test_decode_hold,0);wait_visible(0);
 CHECK(SDL_GetAtomicInt(&test_decode_count)==1);gif_texture(0,root,0);
 for(int frame=1;frame<3;frame++){
  spray_animate(0,spray_animations[0].started+(frame==1?100:300));CHECK(spray_animations[0].frame!=frame);
  Uint64 until=SDL_GetTicks()+3000;while(spray_animations[0].frame!=frame&&SDL_GetTicks()<until){async_update();SDL_Delay(1);}
  CHECK(spray_animations[0].frame==frame);gif_texture(0,root,frame);
 }
 for(int i=0;i<10;i++)CHECK(halo_spray_image_enqueue(0,bytes,size,hash,"",0));
 CHECK(SDL_GetAtomicInt(&test_decode_count)==1);
 CHECK(halo_spray_image_enqueue(1,bytes,size,hash,"",0));wait_visible(1);
 CHECK(spray_slot_images[0]==spray_slot_images[1]&&SDL_GetAtomicInt(&test_decode_count)==1);
 SDL_free(bytes);
 SDL_snprintf(path,sizeof(path),"%s/maximum.png",root);bytes=SDL_LoadFile(path,&size);CHECK(bytes);crypto_blake2b(hash,32,bytes,size);
 int before_uploads=upload_updates;began=SDL_GetTicksNS();CHECK(halo_spray_image_enqueue(2,bytes,size,hash,"",0));
 double png_enqueue_ms=(SDL_GetTicksNS()-began)/1e6;wait_visible(2);
 CHECK(upload_updates-before_uploads>=85);CHECK(SDL_GetAtomicInt(&test_decode_count)==2);
 GLint bound,active;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
 glBindTexture(GL_TEXTURE_2D,spray_textures[2]);unsigned char mip[4];glGetTexImage(GL_TEXTURE_2D,12,GL_RGBA,GL_UNSIGNED_BYTE,mip);
 CHECK(mip[0]==255&&!mip[1]&&!mip[2]&&mip[3]==255);glBindTexture(GL_TEXTURE_2D,bound);glActiveTexture(active);
 // A valid header with corrupt pixel data must never become visible or saved.
 unsigned char bad_hash[32];crypto_blake2b(bad_hash,32,bytes,64);CHECK(halo_spray_image_enqueue(2,bytes,64,bad_hash,"invalid",1));
 Uint64 until=SDL_GetTicks()+5000;while(spray_jobs[2].state!=6&&SDL_GetTicks()<until){async_update();SDL_Delay(1);}
 CHECK(spray_jobs[2].state==6&&!spray_visible[2]);
 // The GPU cap evicts a slot. Repeating that same digest must reload it.
 CHECK(halo_spray_image_enqueue(2,bytes,size,hash,"",0));wait_visible(2);
 CHECK(halo_spray_image_enqueue(4,bytes,size,hash,"",0));wait_visible(4);CHECK(!spray_slot_images[2]);
 CHECK(halo_spray_image_enqueue(2,bytes,size,hash,"",0));wait_visible(2);CHECK(!spray_slot_images[4]);
 size_t residency=0;for(int i=0;i<SPRAY_SHARE_SLOTS;i++)residency+=spray_texture_bytes[i];CHECK(residency<=SPRAY_TEXTURE_LIMIT);
 SDL_free(bytes);
 // Cancel an in-flight decode. The completed old generation cannot reappear.
 SDL_snprintf(path,sizeof(path),"%s/photo.jpg",root);bytes=SDL_LoadFile(path,&size);CHECK(bytes);crypto_blake2b(hash,32,bytes,size);
 SDL_SetAtomicInt(&test_decode_entered,0);SDL_SetAtomicInt(&test_decode_hold,1);
 CHECK(halo_spray_image_enqueue(3,bytes,size,hash,"",0));wait_entered();halo_spray_image_forget();SDL_SetAtomicInt(&test_decode_hold,0);
 until=SDL_GetTicks()+5000;while(SDL_GetAtomicInt(&test_decode_active)&&SDL_GetTicks()<until)SDL_Delay(1);
 CHECK(!SDL_GetAtomicInt(&test_decode_active));for(int i=0;i<10;i++){async_update();SDL_Delay(1);}
 CHECK(!spray_textures[3]&&!spray_slot_images[3]&&spray_jobs[3].state==0);
 // Preload the local file. Acquiring a snapshot then requires no file access.
 char local_path[4096];CHECK(spray_source_path(local_path,sizeof(local_path)));CHECK(SDL_SaveFile(local_path,bytes,size));SDL_free(bytes);
 spray_source_check_tick=0;async_update();struct halo_spray_source source={0};
 until=SDL_GetTicks()+5000;int status;while((status=halo_spray_source_acquire(&source))!=1&&SDL_GetTicks()<until){async_update();SDL_Delay(1);}
 CHECK(status==1&&!memcmp(source.hash,hash,32));halo_spray_source_release(&source);CHECK(SDL_RemovePath(local_path));
 CHECK(halo_spray_source_acquire(&source)==1&&!memcmp(source.hash,hash,32));halo_spray_source_release(&source);
 int before=SDL_GetAtomicInt(&test_decode_count);CHECK(spray_image_enqueue_local(&aspect));wait_visible(0);CHECK(SDL_GetAtomicInt(&test_decode_count)==before);
 halo_spray_image_forget();halo_spray_worker_shutdown();CHECK(!spray_decode_bytes);
 printf("ASYNC first GIF enqueue %.3f ms; maximum PNG enqueue %.3f ms; worst preparation/upload update %.3f ms; worker barrier, decode reuse, 1 MiB upload cap, GIF pixels/mips, cancellation and source preload passed\n",enqueue_ms,png_enqueue_ms,worst_update);
}
'''

TEST = r'''
static void gif_texture(int slot,const char *root,int frame){
 char path[4096];size_t size;SDL_snprintf(path,sizeof(path),"%s/frame%d.rgba",root,frame);unsigned char *expected=SDL_LoadFile(path,&size);CHECK(expected&&size==1024);
 unsigned char actual[1024];GLint active,bound;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
 glBindTexture(GL_TEXTURE_2D,spray_textures[slot]);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,actual);glBindTexture(GL_TEXTURE_2D,bound);glActiveTexture(active);
 for(int i=0;i<256;i++){CHECK(actual[i*4+3]==expected[i*4+3]);if(expected[i*4+3])CHECK(!memcmp(actual+i*4,expected+i*4,3));}SDL_free(expected);
}
static struct halo_spray_clip_vertex quad[6]={
 {{-1,-1,.3f,1},{0,1}},{{1,-1,.3f,1},{1,1}},{{1,1,.3f,1},{1,0}},
 {{-1,-1,.3f,1},{0,1}},{{1,1,.3f,1},{1,0}},{{-1,1,.3f,1},{0,0}}};
static void image(const char *root,const char *name){
 char from[4096],to[4096];size_t size;void *data;
 SDL_snprintf(from,sizeof(from),"%s/%s",root,name);CHECK(spray_source_path(to,sizeof(to)));
 data=SDL_LoadFile(from,&size);CHECK(data);CHECK(SDL_SaveFile(to,data,size));SDL_free(data);
}
int main(int argc,char **argv){
 CHECK(argc==2);CHECK(SDL_Init(SDL_INIT_VIDEO));
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,5);
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
 SDL_Window *w=SDL_CreateWindow("spray test",64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);CHECK(w);
 SDL_GLContext c=SDL_GL_CreateContext(w);CHECK(c);
#define LOAD(name) name=(__typeof__(name))SDL_GL_GetProcAddress(#name);CHECK(name);
 GL_FUNCTIONS(LOAD)
#undef LOAD
 glClipControl(GL_UPPER_LEFT,GL_ZERO_TO_ONE);
 GLuint fb,color,depth,pbo;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
 glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
 glGenTextures(1,&depth);glBindTexture(GL_TEXTURE_2D,depth);glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT32F,64,64,0,GL_DEPTH_COMPONENT,GL_FLOAT,NULL);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth,0);
 CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 glViewport(0,0,64,64);glDepthRange(0,1);glClearColor(.1f,.2f,.3f,.4f);glClearDepth(.4);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 float aspect;image(argv[1],"rgba.png");
 glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);glBufferData(GL_PIXEL_UNPACK_BUFFER,32,NULL,GL_STREAM_DRAW);
 glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,9);glPixelStorei(GL_UNPACK_SKIP_ROWS,2);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
 glActiveTexture(GL_TEXTURE3);CHECK(spray_image_load(&aspect));CHECK(aspect==1);
 GLint v;glGetIntegerv(GL_ACTIVE_TEXTURE,&v);CHECK(v==GL_TEXTURE3);
 glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&v);CHECK(v==(GLint)pbo);
 glGetIntegerv(GL_UNPACK_ALIGNMENT,&v);CHECK(v==8);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&v);CHECK(v==9);
 glGetIntegerv(GL_UNPACK_SKIP_ROWS,&v);CHECK(v==2);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&v);CHECK(v==3);
 glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
 glDisable(GL_DEPTH_TEST);glDepthFunc(GL_GREATER);glDepthMask(1);glEnable(GL_STENCIL_TEST);glEnable(GL_CULL_FACE);glDisable(GL_BLEND);
 glBlendFuncSeparate(GL_ONE,GL_ZERO,GL_ZERO,GL_ONE);glBlendEquationSeparate(GL_MAX,GL_MIN);glColorMask(0,1,0,1);
 halo_spray_image_draw(quad,6);
 CHECK(!glIsEnabled(GL_DEPTH_TEST)&&glIsEnabled(GL_STENCIL_TEST)&&glIsEnabled(GL_CULL_FACE)&&!glIsEnabled(GL_BLEND));
 glGetIntegerv(GL_DEPTH_FUNC,&v);CHECK(v==GL_GREATER);glGetIntegerv(GL_ACTIVE_TEXTURE,&v);CHECK(v==GL_TEXTURE3);
 glGetIntegerv(GL_BLEND_SRC_RGB,&v);CHECK(v==GL_ONE);glGetIntegerv(GL_BLEND_DST_ALPHA,&v);CHECK(v==GL_ONE);
 glGetIntegerv(GL_BLEND_EQUATION_RGB,&v);CHECK(v==GL_MAX);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&v);CHECK(v==GL_MIN);
 GLboolean mask[4];glGetBooleanv(GL_COLOR_WRITEMASK,mask);CHECK(!mask[0]&&mask[1]&&!mask[2]&&mask[3]);glGetBooleanv(GL_DEPTH_WRITEMASK,mask);CHECK(mask[0]);
 unsigned char p[4];glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);CHECK(p[0]>245&&p[1]<5&&p[2]<5&&p[3]==102);
 glReadPixels(48,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);CHECK(p[0]>=25&&p[0]<=26&&p[1]==51&&p[2]>=76&&p[2]<=77&&p[3]==102);
 glReadPixels(16,48,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);CHECK(p[2]>245&&p[0]<5&&p[3]==102);
 float z;glReadPixels(16,16,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&z);CHECK(z>.399f&&z<.401f);
 glColorMask(1,1,1,1);glDepthMask(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 for(int i=0;i<6;i++)quad[i].position[2]=.6f;
 halo_spray_image_draw(quad,6);glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);CHECK(p[0]>=25&&p[0]<=26&&p[1]==51&&p[2]>=76&&p[2]<=77);
 GLuint prior=spray_textures[0];image(argv[1],"invalid.png");CHECK(!spray_image_load(&aspect));CHECK(spray_textures[0]==prior);
 image(argv[1],"large.png");CHECK(!spray_image_load(&aspect));CHECK(spray_textures[0]==prior);
 image(argv[1],"encoded-large.png");CHECK(!spray_image_load(&aspect));CHECK(spray_textures[0]==prior);
 void *bytes=NULL;size_t byte_count=0;CHECK(!halo_spray_file_read(&bytes,&byte_count)&&!bytes&&!byte_count);
 image(argv[1],"rgba.png");CHECK(halo_spray_file_read(&bytes,&byte_count));
 CHECK(spray_image_load_bytes(2,bytes,byte_count,&aspect)&&aspect==1);
 GLuint memory_texture=spray_textures[2];CHECK(memory_texture);
 for(int i=0;i<6;i++)quad[i].position[2]=.3f;
 glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);halo_spray_image_draw_slot(2,quad,6);
 glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);CHECK(p[0]>245&&p[1]<5&&p[2]<5);
 CHECK(!spray_image_load_bytes(-1,bytes,byte_count,&aspect));
 CHECK(!spray_image_load_bytes(SPRAY_SHARE_SLOTS,bytes,byte_count,&aspect));
 CHECK(!spray_image_load_bytes(2,NULL,byte_count,&aspect));
 CHECK(!spray_image_load_bytes(2,bytes,0,&aspect));
 CHECK(!spray_image_load_bytes(2,bytes,SPRAY_SHARE_LIMIT+1,&aspect));
 CHECK(!spray_image_load_bytes(2,"bad",3,&aspect)&&spray_textures[2]==memory_texture);
 char saved[4096],again[4096];CHECK(halo_spray_file_save(bytes,byte_count,"../../profile/:evil",saved,sizeof(saved),&aspect));
 CHECK(strstr(saved,"/spray_profileevil_")&&!strstr(saved,".."));
 CHECK(halo_spray_file_save(bytes,byte_count,"../../profile/:evil",again,sizeof(again),&aspect));CHECK(!strcmp(saved,again));
 unsigned char hash[32];crypto_blake2b(hash,32,bytes,byte_count);void *cached=malloc(byte_count);CHECK(cached);
 CHECK(halo_spray_file_cached(hash,byte_count,cached)&&!memcmp(bytes,cached,byte_count));
 CHECK(!halo_spray_file_cached(hash,byte_count-1,cached));
 FILE *changed=fopen(saved,"r+b");CHECK(changed);CHECK(fputc(0,changed)!=EOF);CHECK(!fclose(changed));
 CHECK(!halo_spray_file_cached(hash,byte_count,cached));
 CHECK(halo_spray_file_save(bytes,byte_count,"repair",again,sizeof(again),&aspect));CHECK(strcmp(saved,again));
 CHECK(halo_spray_file_cached(hash,byte_count,cached));
 char index[4096],directory[4096];CHECK(spray_cache_paths(hash,index,sizeof(index),directory,sizeof(directory)));
 CHECK(SDL_SaveFile(index,"../outside.png",14));CHECK(!halo_spray_file_cached(hash,byte_count,cached));
 CHECK(halo_spray_file_save(bytes,byte_count,"repair",saved,sizeof(saved),&aspect));free(cached);
 size_t received_count;void *received=SDL_LoadFile(saved,&received_count);CHECK(received&&received_count==byte_count&&!memcmp(received,bytes,byte_count));SDL_free(received);
 unsigned char *legacy=malloc(byte_count+1),*legacy_copy=malloc(byte_count+1);CHECK(legacy&&legacy_copy);memcpy(legacy,bytes,byte_count);legacy[byte_count]=0;
 char legacy_path[4096];SDL_snprintf(legacy_path,sizeof(legacy_path),"%s/spray_legacy.png",directory);CHECK(SDL_SaveFile(legacy_path,legacy,byte_count+1));
 crypto_blake2b(hash,32,legacy,byte_count+1);CHECK(halo_spray_file_cached(hash,byte_count+1,legacy_copy)&&!memcmp(legacy,legacy_copy,byte_count+1));
 CHECK(halo_spray_file_save(legacy,byte_count+1,"legacy",again,sizeof(again),&aspect)&&!strcmp(again,legacy_path));free(legacy);free(legacy_copy);
 CHECK(spray_image_load_slot(1,saved,&aspect));CHECK(spray_textures[1]&&spray_textures[0]==prior);
 CHECK(!spray_image_load_slot(-1,saved,&aspect)&&!spray_image_load_slot(SPRAY_SHARE_SLOTS,saved,&aspect));
 CHECK(!halo_spray_file_save("bad",3,"bad",again,sizeof(again),&aspect));halo_spray_file_free(bytes);CHECK(!spray_decode_bytes);
 CHECK(SDL_RemovePath(saved));CHECK(SDL_RemovePath(again));
 image(argv[1],"encoded-within-limit.png");CHECK(spray_image_load(&aspect));
 CHECK(halo_spray_file_read(&bytes,&byte_count));CHECK(byte_count>2u*1024u*1024u&&byte_count<=SPRAY_SHARE_LIMIT);
 CHECK(spray_image_load_bytes(3,bytes,byte_count,&aspect));halo_spray_file_free(bytes);
 image(argv[1],"maximum.png");CHECK(halo_spray_file_read(&bytes,&byte_count));
 CHECK(halo_spray_image_aspect(bytes,byte_count,&aspect)&&aspect==1);
 CHECK(spray_image_load_bytes(4,bytes,byte_count,&aspect)&&aspect==1);CHECK(!spray_decode_bytes);
 CHECK(spray_texture_bytes[4]>64u*1024u*1024u&&spray_texture_bytes[4]<=SPRAY_TEXTURE_LIMIT);
 glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);halo_spray_image_draw_slot(4,quad,6);
 glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);CHECK(p[0]>245&&p[1]<5&&p[2]<5);
 CHECK(spray_image_load_bytes(5,bytes,byte_count,&aspect));CHECK(!spray_textures[4]&&spray_textures[5]);
 size_t resident=0;for(int i=0;i<SPRAY_SHARE_SLOTS;i++)resident+=spray_texture_bytes[i];CHECK(resident<=SPRAY_TEXTURE_LIMIT);
 halo_spray_file_free(bytes);GLuint maximum_texture=spray_textures[5];
 image(argv[1],"too-tall.png");CHECK(!spray_image_load(&aspect)&&spray_textures[5]==maximum_texture);CHECK(!spray_decode_bytes);
 const char *formats[]={"rgb.png","palette.png","gray.png","rgba.png"};
 for(int i=0;i<4;i++){image(argv[1],formats[i]);CHECK(spray_image_load(&aspect));CHECK(aspect==1);}
 image(argv[1],"photo.jpg");CHECK(halo_spray_file_read(&bytes,&byte_count));CHECK(spray_image_load_bytes(6,bytes,byte_count,&aspect));
 CHECK(halo_spray_file_save(bytes,byte_count,"jpeg",saved,sizeof(saved),&aspect));CHECK(!strcmp(strrchr(saved,'.'),".jpg"));halo_spray_file_free(bytes);
 image(argv[1],"animated.gif");CHECK(halo_spray_file_read(&bytes,&byte_count));
 int width,height,frames,*delays=NULL;unsigned char *decoded=spray_decode_image(bytes,byte_count,&width,&height,&frames,&delays);
 CHECK(decoded&&width==16&&height==16&&frames==3&&delays[0]==100&&delays[1]==200&&delays[2]==300);
 for(int i=0;i<frames;i++){char reference[4096];size_t reference_size;SDL_snprintf(reference,sizeof(reference),"%s/frame%d.rgba",argv[1],i);
  unsigned char *expected=SDL_LoadFile(reference,&reference_size);CHECK(expected&&reference_size==16*16*4);
  for(int n=0;n<16*16;n++){CHECK(decoded[(i*256+n)*4+3]==expected[n*4+3]);if(expected[n*4+3])CHECK(!memcmp(decoded+(i*256+n)*4,expected+n*4,3));}SDL_free(expected);}
 stbi_image_free(decoded);stbi_image_free(delays);
 CHECK(spray_image_load_bytes(7,bytes,byte_count,&aspect));CHECK(spray_animations[7].frames==3);
 gif_texture(7,argv[1],0);
 CHECK(halo_spray_file_save(bytes,byte_count,"gif",saved,sizeof(saved),&aspect));CHECK(!strcmp(strrchr(saved,'.'),".gif"));
 spray_animate(7,spray_animations[7].started+100);CHECK(spray_animations[7].frame==1);
 gif_texture(7,argv[1],1);
 spray_animate(7,spray_animations[7].started+300);CHECK(spray_animations[7].frame==2);
 gif_texture(7,argv[1],2);
 spray_animate(7,spray_animations[7].started+600);CHECK(spray_animations[7].frame==0);
 gif_texture(7,argv[1],0);
 GLuint gif_texture=spray_textures[7];CHECK(!spray_image_load_bytes(7,bytes,byte_count-1,&aspect)&&spray_textures[7]==gif_texture);
 halo_spray_file_free(bytes);
 image(argv[1],"too-many.gif");CHECK(!spray_image_load(&aspect));
 image(argv[1],"animation-large.gif");CHECK(!spray_image_load(&aspect));
 image(argv[1],"too-wide.gif");CHECK(!spray_image_load(&aspect));
 image(argv[1],"resident.gif");CHECK(halo_spray_file_read(&bytes,&byte_count));
 CHECK(spray_image_load_bytes(8,bytes,byte_count,&aspect));CHECK(spray_image_load_bytes(9,bytes,byte_count,&aspect));CHECK(spray_image_load_bytes(10,bytes,byte_count,&aspect));
 size_t animation_resident=0;for(int i=0;i<SPRAY_SHARE_SLOTS;i++)animation_resident+=spray_animation_size(i);CHECK(animation_resident<=SPRAY_ANIMATION_LIMIT);
 CHECK(!spray_animations[8].pixels&&spray_animations[9].pixels&&spray_animations[10].pixels);halo_spray_file_free(bytes);
 halo_spray_image_forget();for(int i=0;i<SPRAY_SHARE_SLOTS;i++)CHECK(!spray_textures[i]);halo_spray_image_forget();
 CHECK(!spray_decode_bytes);
 char path[4096];CHECK(spray_source_path(path,sizeof(path)));CHECK(SDL_RemovePath(path));CHECK(!spray_image_load(&aspect));
 char fixture[4096],jpg[4096],gif[4096];size_t source_size;void *source_bytes;
 SDL_snprintf(fixture,sizeof(fixture),"%s/photo.jpg",argv[1]);source_bytes=SDL_LoadFile(fixture,&source_size);CHECK(source_bytes);strcpy(jpg,path);strcpy(strrchr(jpg,'.'),".jpg");CHECK(SDL_SaveFile(jpg,source_bytes,source_size));SDL_free(source_bytes);
 CHECK(spray_image_load(&aspect));CHECK(halo_spray_file_read(&bytes,&byte_count));CHECK(!strcmp(spray_image_extension(bytes,byte_count),"jpg"));halo_spray_file_free(bytes);
 SDL_snprintf(fixture,sizeof(fixture),"%s/animated.gif",argv[1]);source_bytes=SDL_LoadFile(fixture,&source_size);CHECK(source_bytes);strcpy(gif,path);strcpy(strrchr(gif,'.'),".gif");CHECK(SDL_SaveFile(gif,source_bytes,source_size));SDL_free(source_bytes);
 CHECK(spray_find_source_path(fixture,sizeof(fixture))&&!strcmp(fixture,jpg));CHECK(SDL_RemovePath(jpg));CHECK(spray_image_load(&aspect)&&spray_animations[0].frames==3);CHECK(SDL_RemovePath(gif));
 halo_spray_image_forget();CHECK(!spray_decode_bytes);
 CHECK(glGetError()==GL_NO_ERROR);CHECK(invalidations);SDL_GL_DestroyContext(c);SDL_DestroyWindow(w);SDL_Quit();puts("Spray PNG/JPEG/GIF, cache and GL regressions passed");return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default=str(ROOT / '.toolchain/llvm/LLVM/bin/clang.exe') if sys.platform == 'win32' else 'clang')
    args = parser.parse_args()
    source = (ROOT / 'port/linux/src/spray_image.c').read_text().replace('#include "xgpu.h"', '')
    source = source.replace('void console_printf(BOOL warning, const char *format, ...);', '')
    async_source = (ROOT / 'port/linux/src/spray_async.h').read_text()
    async_source = async_source.replace('decoded = spray_decode_image(', 'spray_test_before_decode(); decoded = spray_decode_image(')
    async_source = async_source.replace('if (!decoded) {', 'SDL_AddAtomicInt(&test_decode_active,-1); if (!decoded) {')
    async_source = async_source.replace('glTexSubImage2D(', 'spray_test_sub_image(')
    source = source.replace('#include "spray_async.h"', async_source)
    source = source.replace('"../../third_party/stb/stb_image.h"', '"stb_image.h"')
    source = source.replace('"../../third_party/monocypher/monocypher.h"', '"monocypher.h"')
    command = [args.cc, '-std=gnu11', '-O2', '-fuse-ld=lld', '-I' + str(ROOT / 'port/linux/src'), '-I' + str(ROOT / 'port/third_party/stb')]
    command += ['-I' + str(ROOT / 'port/third_party/monocypher'), str(ROOT / 'port/third_party/monocypher/monocypher.c')]
    env = dict(os.environ)
    if sys.platform == 'win32':
        sdl = next((ROOT / 'build/windows/third_party').glob('SDL3-*/include')).parent
        command += ['--target=i686-pc-windows-msvc', '-I' + str(sdl / 'include'), '-I' + str(ROOT / 'port/windows/include/posix')]
        libraries = [str(sdl / 'lib/x86/SDL3.lib')]
        env['PATH'] = str(sdl / 'lib/x86') + os.pathsep + env['PATH']
    else:
        command += subprocess.check_output(['pkg-config', '--cflags', 'sdl3'], text=True).split()
        libraries = subprocess.check_output(['pkg-config', '--libs', 'sdl3'], text=True).split()
    with tempfile.TemporaryDirectory(prefix='halo-spray-test-') as folder:
        path = Path(folder)
        image = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
        image.paste((255, 0, 0, 255), (0, 0, 8, 8)); image.paste((0, 0, 255, 255), (0, 8, 8, 16))
        image.save(path / 'rgba.png')
        for mode, name in [('RGB', 'rgb'), ('P', 'palette'), ('L', 'gray')]:
            image.convert(mode).save(path / f'{name}.png')
        (path / 'invalid.png').write_bytes(b'not a png')
        header = (ROOT / 'port/linux/include/spray_share.h').read_text()
        limit_mib = int(re.search(r'#define SPRAY_SHARE_LIMIT \((\d+)u \* 1024u \* 1024u\)', header)[1])
        (path / 'encoded-large.png').write_bytes(b'x' * (limit_mib * 1024 * 1024 + 1))
        Image.new('RGBA', (1024, 1200), (255, 0, 0, 255)).save(path / 'encoded-within-limit.png', compress_level=0)
        assert 2 * 1024 * 1024 < (path / 'encoded-within-limit.png').stat().st_size <= limit_mib * 1024 * 1024
        Image.new('RGBA', (4096, 4096), (255, 0, 0, 255)).save(path / 'maximum.png')
        Image.new('RGB', (4097, 1)).save(path / 'large.png')
        Image.new('RGB', (1, 4097)).save(path / 'too-tall.png')
        image.convert('RGB').save(path / 'photo.jpg', quality=95)
        palette = [0,0,0,255,0,0,0,255,0,0,0,255] + [0]*756
        first = Image.new('P', (16,16));first.putpalette(palette);first.paste(1,(0,0,8,16))
        second = first.copy();second.paste(2,(8,0,16,8))
        third = first.copy();third.paste(3,(8,8,16,16))
        first.save(path/'animated.gif',save_all=True,append_images=[second,third],duration=[100,200,300],loop=0,disposal=[1,3,2],transparency=0,optimize=False)
        with Image.open(path/'animated.gif') as gif:
            for i in range(gif.n_frames):
                gif.seek(i);(path/f'frame{i}.rgba').write_bytes(gif.convert('RGBA').tobytes())
        tiny = [Image.new('RGB',(1,1),(i%256,0,0)) for i in range(257)]
        tiny[0].save(path/'too-many.gif',save_all=True,append_images=tiny[1:],duration=100,optimize=False)
        huge = [Image.new('RGB',(2048,2048),(i*40,0,0)) for i in range(5)]
        huge[0].save(path/'animation-large.gif',save_all=True,append_images=huge[1:],duration=100,optimize=False)
        huge[0].save(path/'resident.gif',save_all=True,append_images=huge[1:2],duration=100,optimize=False)
        Image.new('RGB',(4097,1)).save(path/'too-wide.gif')
        if sys.platform == 'win32':
            threads = (ROOT / 'port/windows/src/win32_posix.c').read_text().split('/* ---------- threads */')[1].split('/* ---------- time */')[0]
            threads = threads.replace(block(threads, 'int pthread_cond_timedwait('), '')
            (path / 'threads.c').write_text('#include <windows.h>\n#include <stdlib.h>\n#include <errno.h>\n#include "pthread.h"\n' + threads)
            command.append(str(path / 'threads.c'))
        (path / 'spray.c').write_text(PREFIX + ASYNC_HOOKS + source + ASYNC_TEST + TEST.replace('CHECK(glGetError()==GL_NO_ERROR);', 'run_async(argv[1]);CHECK(glGetError()==GL_NO_ERROR);'))
        subprocess.run([*command, str(path / 'spray.c'), *libraries, '-o', str(path / 'spray.exe')], check=True)
        subprocess.run([str(path / 'spray.exe'), str(path)], cwd=ROOT, env=env, check=True, timeout=60)


if __name__ == '__main__':
    main()
