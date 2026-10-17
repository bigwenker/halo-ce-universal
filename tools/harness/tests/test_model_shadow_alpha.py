"""Production shadow draws preserve opaque silhouettes and alpha-cutout holes."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,function,enum_with,run,mutated,CHECK_FAILED

def executable(fault=None):
    source=read('source/rasterizer/xbox/rasterizer_xbox_shadows.c')
    code=function(source,'_rasterizer_environment_shadow_model_draw')
    if fault=='opaque':
        code=mutated(code,'!TEST_FLAG(model->flags, _shader_model_not_alpha_tested_bit));','TRUE);')
    elif fault=='cutout':
        code=mutated(code,'!TEST_FLAG(model->flags, _shader_model_not_alpha_tested_bit));','FALSE);')
    return build('model_shadow_alpha',(('flags.inc',enum_with(source,'_shader_model_not_alpha_tested_bit')),('under_test.inc',code)))

@pytest.mark.parametrize('case',['opaque','cutout-solid','cutout-hole','mixed','disabled','other-target','other-shader','two-sided'])
def test_shadow_coverage(case):
    status,out=run(executable(),case)
    assert status==0,out

@pytest.mark.parametrize('fault,case',[('opaque','opaque'),('cutout','cutout-hole')])
def test_negative_control(fault,case):
    status,out=run(executable(fault),case)
    assert status==CHECK_FAILED,out
