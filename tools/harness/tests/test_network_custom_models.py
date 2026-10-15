"""Production optional model-ID admission, including a forged-owner control."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,function,run,mutated,CHECK_FAILED
def executable(fault=False):
    source=read('port/linux/game/network_custom_models.c')
    code=function(source,'identity')+'\n'+function(source,'custom_models_network_handle')
    if fault:code=mutated(code,'(host && p->network_player_data.machine_index!=machine)','FALSE')
    return build('network_custom_models',(('under_test.inc',code),('id.inc',function(read('port/linux/src/custom_model_files.c'),'halo_custom_model_id_valid').replace('HALO_CUSTOM_MODEL_ID','32'))))
@pytest.mark.parametrize('case',['valid','sender','reliable','truncated','path','unterminated','stale','client','local'])
def test_case(case):
    code,out=run(executable(),case)
    assert code==0,out
def test_negative_owner_control():
    assert run(executable(True),'sender')[0]==CHECK_FAILED
