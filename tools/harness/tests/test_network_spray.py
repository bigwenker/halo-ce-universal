"""Real host admission: owner, live unit, range, BSP, cooldown, finite vectors."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, run, function, mutated, CHECK_FAILED, structure

def generated(old_free=False):
    headers=read('port/linux/include/spray_share.h')+read('port/third_party/monocypher/monocypher.h')
    headers+=structure(read('port/linux/include/halo_spray.h'),'halo_spray_source')
    core=re.sub(r'^#include "[^\n]+\n','',read('port/linux/src/spray_share.c'),flags=re.M)
    crypto=read('port/third_party/monocypher/monocypher.c').replace('#include "monocypher.h"','')
    adapter=re.sub(r'^#include[^\n]+\n','',read('port/linux/game/network_spray.c'),flags=re.M)
    if old_free:
        adapter=mutated(adapter,'halo_spray_source_release(&source);','free((void *)source.data);')
    provider=read('port/linux/src/spray_image.c')
    code=function(provider,'halo_spray_file_read')+'\n'
    code+=function(provider,'halo_spray_file_free')+'\n'
    code+='''
int halo_spray_source_acquire(struct halo_spray_source *source) {
 void *data; memset(source,0,sizeof(*source));
 if(!halo_spray_file_read(&data,&source->size))return 0;
 source->data=data;crypto_blake2b(source->hash,32,data,source->size);return 1;
}
void halo_spray_source_release(struct halo_spray_source *source){halo_spray_file_free((void *)source->data);}
'''
    # Retain the real cseries free macros at the engine/platform boundary.
    macros='\n'.join(re.findall(r'^#define (?:match_free\(|free\().*$',read('source/cseries/cseries.h'),re.M))
    return (('api.inc',headers),('provider.inc',code),
            ('under_test.inc',crypto+core+'\n'+macros+'\n'+adapter+'\n#undef free\n'))

@pytest.mark.parametrize('case',['valid','transport','owner','dead','blocked','distance','reach','nan','bsp','cooldown','menu'])
def test_host_admission(case):
    status,output=run(build('network_spray',generated()),case)
    assert status==0,output

@pytest.mark.parametrize('case',['publish-host','publish-client','publish-rejected',
                                'publish-ready-failed','publish-missing','publish-unavailable'])
def test_publish_ownership(case):
    status,output=run(build('network_spray',generated()),case)
    assert status==0,output

def test_original_wrong_allocator_detected():
    status,output=run(build('network_spray',generated(True)),'publish-host')
    assert status==CHECK_FAILED and 'engine allocator' in output,output
