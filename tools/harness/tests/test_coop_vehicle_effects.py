"""Host-only extra-vehicle effects, ordering and malformed packets."""
import re,sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,run
@pytest.mark.parametrize('case',['host','pending','invalid','expired'])
def test_case(case):
    code=re.sub(r'^#include.*\n','',read('port/linux/game/coop_vehicle_effects.c'),flags=re.M)
    status,output=run(build('coop_vehicle_effects',( ('under_test.inc',code),)),case)
    assert status==0,output
