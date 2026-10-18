"""FP selection is per player, optional, and independent of body/weapon tags."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import read,function,build,run


def executable():
    code=function(read('port/linux/game/custom_models.c'),'custom_models_first_person_model')
    return build('custom_model_first_person',(('selection.inc',code),))


@pytest.mark.parametrize('case',['selected','missing','wrong_model','npc','stale','chief','other_profile'])
def test_case(case):
    status,output=run(executable(),case)
    assert status==0,output
