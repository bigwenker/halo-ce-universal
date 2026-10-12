"""Production instant-headshot branch with the reduced-friendly admission flag."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,function,run,mutated,CHECK_FAILED

def generated(fault=False):
    source=function(read('source/objects/damage.c'),'object_damage_body')
    start=source.index('if (damage_amount > 0.f && TEST_FLAG')
    end=source.index('\n\t\tobject->object.body_vitality -= actual_damage;',start)
    code=source[start:end]
    if fault:code=mutated(code,'allow_instant_kill && ','')
    return (('under_test.inc',code),)

@pytest.mark.parametrize('fault',[False,True])
def test_headshot(fault):
    status,output=run(build('friendly_headshots',generated(fault)), 'headshot')
    assert status==(CHECK_FAILED if fault else 0),output
