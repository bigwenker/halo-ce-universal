"""Production RGB storage/parser and continuous HSV conversion."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,run,mutated,CHECK_FAILED

def generated(fault=False):
    code=re.sub(r'^#include[^\n]*\n','',read('port/linux/game/player_profile_rgb.c'),flags=re.M)
    if fault:
        code=mutated(code,"if (*text) return FALSE;","/* accept trailing garbage */")
    palette=re.search(r'static long profile_color_table[^=]+=\s*(\{[^}]+\})',read('source/saved games/player_profile.c')).group(1)
    return (('palette.inc',f'static unsigned long palette[]={palette};'),('under_test.inc',code))

@pytest.mark.parametrize('case',['parse','hsv','storage','fallback','nearest','local'])
def test_case(case):
    status,out=run(build('profile_rgb',generated()),case)
    assert status==0,out

def test_negative_trailing():
    status,out=run(build('profile_rgb',generated(True)),'parse')
    assert status==CHECK_FAILED,out
