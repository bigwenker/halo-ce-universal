"""Bounds-checked extraction of the two stock respawn samples."""
import re,sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,function,run
@pytest.mark.parametrize('case',['samples','bounds','cleanup'])
def test_case(case):
    s=read('port/linux/game/coop_respawn_audio.c')
    names=re.search(r'static char const \*respawn_sound_names\[2\] = \{.*?\};',s,re.S).group()
    code=names+'\n'+'\n'.join(function(s,n) for n in ('audio_u32','audio_pointer','audio_samples','audio_release'))
    status,output=run(build('coop_respawn_audio',( ('under_test.inc',code),)),case)
    assert status==0,output
