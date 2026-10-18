"""Map unload and failed imports respect the game's allocator contract."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import read,function,build,run,mutated,CHECK_FAILED

def executable(fault=False):
    code=function(read('port/linux/game/custom_models.c'),'asset_dispose')
    if fault:code=mutated(code,'if(asset->vertex_data[j])','')
    return build('custom_models_cleanup',(('cleanup.inc',code),))

@pytest.mark.parametrize('case',['empty','partial'])
def test_case(case):
    status,output=run(executable(),case)
    assert status==0,output

def test_negative_null_free():
    assert run(executable(True),'empty')[0]==CHECK_FAILED
