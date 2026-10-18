"""Decode PNG and indexed/RGB BMP through the actual custom-model image path."""
import re
import sys
from pathlib import Path
import pytest
import struct,subprocess,zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import ROOT,build,read,function,run

def executable():
    source=read('port/linux/src/custom_model_files.c')
    formats='\n'.join(re.findall(r'^#define STBI_ONLY_.*$',read('port/linux/src/spray_image.c'),re.M))
    decoder=formats+'\n#define STBI_NO_STDIO\n#define STBI_NO_LINEAR\n#define STBI_NO_HDR\n#define STBI_MAX_DIMENSIONS 4096\n#define STB_IMAGE_IMPLEMENTATION\n#include "'+str(ROOT/'port/third_party/stb/stb_image.h')+'"\n'
    code=decoder+'\n'+'\n'.join(function(source,n) for n in ('read_bounded','custom_image_u32','custom_bmp_dimensions','halo_custom_image_dispose','custom_image_load_path','halo_custom_image_load','halo_custom_material_image_load'))
    code=function(read('port/linux/src/custom_model_jms.c'),'halo_custom_texture_name_valid')+'\n'+code
    return build('custom_model_images',(('halo_custom_models.h',read('port/linux/include/halo_custom_models.h')),('images.inc',code)))

def bmp(width,height,bits,top_down=False):
    pixels=[[(x*17,y*41,123,255) for x in range(width)] for y in range(height)]
    palette=b''
    if bits==8:
        palette=bytes(v for i in range(256) for v in (i//2,255-i,i,0))
        pixels=[[(y*width+x,255-(y*width+x),(y*width+x)//2,255) for x in range(width)] for y in range(height)]
    stride=((width*bits+31)//32)*4;rows=[]
    for y in range(height):
        row=bytes(range(y*width,(y+1)*width)) if bits==8 else bytes(v for p in pixels[y] for v in ((p[2],p[1],p[0],p[3]) if bits==32 else (p[2],p[1],p[0])))
        rows.append(row+b'\0'*(stride-len(row)))
    data=b''.join(rows if top_down else rows[::-1]);offset=54+len(palette)
    header=b'BM'+struct.pack('<IHHI',offset+len(data),0,0,offset)+struct.pack('<IiiHHIIiiII',40,width,-height if top_down else height,1,bits,0,len(data),0,0,256 if bits==8 else 0,0)
    return header+palette+data,pixels

def png(width,height,pixel):
    def chunk(name,data):return struct.pack('>I',len(data))+name+data+struct.pack('>I',zlib.crc32(name+data)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+bytes(pixel)*width)*height))+chunk(b'IEND',b'')

def check_image(root,kind,pixels):
    result=subprocess.run([str(executable()),str(root),str(kind)],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    w,h,*colors=result.stdout.split();w,h=int(w),int(h);ew,eh=len(pixels[0]),len(pixels)
    want=(128,256) if kind==0 else tuple(1<<(n-1).bit_length() for n in (ew,eh))
    assert (w,h)==want
    for actual,(x,y) in zip(colors,[(0,0),(w//2,h//2),(w-1,h-1)]):
        pixel=pixels[y*eh//h][x*ew//w]
        assert int(actual,16)==(pixel[3]<<24)|(pixel[0]<<16)|(pixel[1]<<8)|pixel[2]

@pytest.mark.parametrize('bits',[8,24,32])
@pytest.mark.parametrize('kind',[1,2])
@pytest.mark.parametrize('top_down',[False,True])
def test_bmp(tmp_path,bits,kind,top_down):
    folder=tmp_path/'model';(folder/'textures').mkdir(parents=True)
    data,pixels=bmp(3,6 if kind==0 else 2,bits,top_down)
    path=folder/('preview.bmp' if kind==0 else 'textures/armor.bmp' if kind==1 else 'textures/armor_mask.bmp');path.write_bytes(data)
    check_image(tmp_path,kind,pixels)

def test_png_priority(tmp_path):
    folder=tmp_path/'model/textures';folder.mkdir(parents=True)
    (folder/'armor.bmp').write_bytes(bmp(2,2,24)[0]);pixel=(0,123,45,120)
    (folder/'armor.png').write_bytes(png(2,2,pixel))
    check_image(tmp_path,1,[[pixel]*2]*2)

@pytest.mark.parametrize('bad',['truncated','dimensions','signature'])
def test_bad_bmp(tmp_path,bad):
    folder=tmp_path/'model/textures';folder.mkdir(parents=True)
    path=folder/'armor.bmp';data=bytearray(bmp(2,2,24)[0])
    if bad=='truncated':data=data[:50]
    elif bad=='dimensions':struct.pack_into('<i',data,18,2147483647)
    else:data[:2]=b'NO'
    path.write_bytes(data)
    assert subprocess.run([str(executable()),str(tmp_path),'1'],capture_output=True).returncode==1

@pytest.mark.parametrize('texture,mask',[('Cloth_d',False),('face_d',False),('Hair_d2',True)])
def test_material_texture(tmp_path,texture,mask):
    folder=tmp_path/'model/textures';folder.mkdir(parents=True)
    name=texture+('_mask' if mask else '')+'.bmp';(folder/name).write_bytes(bmp(3,2,8)[0])
    result=subprocess.run([str(executable()),str(tmp_path),'2' if mask else '1',texture],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    assert result.stdout.startswith('4 2 ')

@pytest.mark.parametrize('texture',['../secret','a/b','a\\b','C:drive','..',''])
def test_unsafe_material_texture(tmp_path,texture):
    assert subprocess.run([str(executable()),str(tmp_path),'1',texture],capture_output=True).returncode==1


def test_fixed_preview_png(tmp_path):
    folder=tmp_path/'model';folder.mkdir()
    pixel=(12,34,56,255)
    (folder/'preview.png').write_bytes(png(2,4,pixel))
    check_image(tmp_path,0,[[pixel]*2]*4)


def test_preview_does_not_use_other_filenames(tmp_path):
    folder=tmp_path/'model';folder.mkdir()
    (folder/'old_portrait.png').write_bytes(png(2,4,(1,2,3,255)))
    (folder/'preview.bmp').write_bytes(bmp(2,4,24)[0])
    assert subprocess.run([str(executable()),str(tmp_path),'0'],capture_output=True).returncode==1
