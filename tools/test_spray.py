"""Real spray PNG loader and GL pass: pixels, occlusion and restored state."""
import argparse
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image
from test_screenshot import PRELUDE

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
TEST = r'''
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
 CHECK(halo_spray_file_save(bytes,byte_count,"../../profile/:evil",again,sizeof(again),&aspect));CHECK(strcmp(saved,again));
 size_t received_count;void *received=SDL_LoadFile(saved,&received_count);CHECK(received&&received_count==byte_count&&!memcmp(received,bytes,byte_count));SDL_free(received);
 CHECK(spray_image_load_slot(1,saved,&aspect));CHECK(spray_textures[1]&&spray_textures[0]==prior);
 CHECK(!spray_image_load_slot(-1,saved,&aspect)&&!spray_image_load_slot(SPRAY_SHARE_SLOTS,saved,&aspect));
 CHECK(!halo_spray_file_save("bad",3,"bad",again,sizeof(again),&aspect));halo_spray_file_free(bytes);CHECK(!spray_decode_bytes);
 CHECK(SDL_RemovePath(saved));CHECK(SDL_RemovePath(again));
 image(argv[1],"encoded-within-limit.png");CHECK(spray_image_load(&aspect));
 CHECK(halo_spray_file_read(&bytes,&byte_count));CHECK(byte_count>2u*1024u*1024u&&byte_count<=SPRAY_SHARE_LIMIT);
 CHECK(spray_image_load_bytes(3,bytes,byte_count,&aspect));halo_spray_file_free(bytes);
 const char *formats[]={"rgb.png","palette.png","gray.png","rgba.png"};
 for(int i=0;i<4;i++){image(argv[1],formats[i]);CHECK(spray_image_load(&aspect));CHECK(aspect==1);}
 halo_spray_image_forget();for(int i=0;i<SPRAY_SHARE_SLOTS;i++)CHECK(!spray_textures[i]);halo_spray_image_forget();
 char path[4096];CHECK(spray_source_path(path,sizeof(path)));CHECK(SDL_RemovePath(path));CHECK(!spray_image_load(&aspect));
 CHECK(glGetError()==GL_NO_ERROR);CHECK(invalidations);SDL_GL_DestroyContext(c);SDL_DestroyWindow(w);SDL_Quit();puts("Spray PNG and GL regressions passed");return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default=str(ROOT / '.toolchain/llvm/LLVM/bin/clang.exe') if sys.platform == 'win32' else 'clang')
    args = parser.parse_args()
    source = (ROOT / 'port/linux/src/spray_image.c').read_text().replace('#include "xgpu.h"', '')
    source = source.replace('void console_printf(BOOL warning, const char *format, ...);', '')
    source = source.replace('"../../third_party/stb/stb_image.h"', '"stb_image.h"')
    command = [args.cc, '-std=gnu11', '-O2', '-fuse-ld=lld', '-I' + str(ROOT / 'port/linux/src'), '-I' + str(ROOT / 'port/third_party/stb')]
    env = dict(os.environ)
    if sys.platform == 'win32':
        sdl = next((ROOT / 'build/windows/third_party').glob('SDL3-*/include')).parent
        command += ['--target=i686-pc-windows-msvc', '-I' + str(sdl / 'include')]
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
        Image.new('RGB', (2049, 1)).save(path / 'large.png')
        (path / 'spray.c').write_text(PREFIX + source + TEST)
        subprocess.run([*command, str(path / 'spray.c'), *libraries, '-o', str(path / 'spray.exe')], check=True)
        subprocess.run([str(path / 'spray.exe'), str(path)], cwd=ROOT, env=env, check=True, timeout=30)


if __name__ == '__main__':
    main()
