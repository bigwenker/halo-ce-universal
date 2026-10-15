"""Authenticated optional RGB messages use production admission code."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,function,run,mutated,CHECK_FAILED

def generated(fault=False):
    source=read('port/linux/game/network_profile_rgb.c')
    code='\n'.join(function(source,name) for name in ('rgb_identity','network_profile_rgb_handle','rgb_apply'))
    if fault: code=mutated(code,'(host && p->network_player_data.machine_index!=machine)','FALSE')
    return (('under_test.inc',code),)

@pytest.mark.parametrize('case',['authenticated','sender','malformed','client','stale','materials','teams'])
def test_case(case):
    status,out=run(build('network_profile_rgb',generated()),case)
    assert status==0,out

def test_negative_sender():
    status,out=run(build('network_profile_rgb',generated(True)),'sender')
    assert status==CHECK_FAILED,out
