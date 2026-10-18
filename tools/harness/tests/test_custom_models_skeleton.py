"""Named bones accept edited rest poses while keeping weights and rig bounds."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import read,function,build,run,mutated,CHECK_FAILED

def executable(fault=False):
    code=function(read('port/linux/game/custom_models.c'),'skeleton_remap')
    if fault:code=mutated(code,'if(v->node[1]>=0)v->node[1]=map[v->node[1]];',';')
    return build('custom_models_skeleton',(('halo_custom_models.h',read('port/linux/include/halo_custom_models.h')),('skeleton.inc',code)))

@pytest.mark.parametrize('case',['rest','order','missing','duplicate','bad-weight-index','stock'])
def test_case(case):
    status,output=run(executable(),case)
    assert status==0,output

def test_negative_remap():
    assert run(executable(True),'order')[0]==CHECK_FAILED
