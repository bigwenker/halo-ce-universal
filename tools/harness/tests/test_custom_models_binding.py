"""Cosmetic bindings never replace unrelated units or recycled objects."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,function,run,mutated,CHECK_FAILED
def executable(fault=False):
    code='\n'.join(function(read('port/linux/game/custom_models.c'),n) for n in ('custom_models_object_created','custom_models_render_model'))
    if fault:code=mutated(code,'objects[slot].handle!=index','FALSE')
    return build('custom_models_binding',(('binding.inc',code),))
@pytest.mark.parametrize('case',['player','missing','npc','weapon','other_model','corpse','reused'])
def test_case(case):
    status,out=run(executable(),case)
    assert status==0,out
def test_negative_stale_handle_control():
    assert run(executable(True),'reused')[0]==CHECK_FAILED
