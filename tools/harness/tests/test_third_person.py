"""Campaign camera eligibility, transitions and local-only free look."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, function, run, mutated, CHECK_FAILED


def generated(fault=None):
    code=re.sub(r'^#include "[^"\n]+"\n','',read('port/linux/game/third_person.c'),flags=re.M)
    code+='\n'+function(read('source/camera/director.c'),'director_choose_game_perspective')
    if fault=='vehicle':
        code=mutated(code,'third_person_camera_wanted(local_player_index) && !following',
                     '!following && third_person_camera_wanted(local_player_index)')
    if fault=='multiplayer':
        code=mutated(code,'!game_engine_running()','TRUE')
    return (('under_test.inc',code),)


@pytest.mark.parametrize('case',['toggle','eligibility','scope','distance','camera','free-look','vehicle-transition'])
def test_case(case):
    status,output=run(build('third_person',generated()),case)
    assert status==0,output


@pytest.mark.parametrize('fault,case',[('vehicle','vehicle-transition'),('multiplayer','eligibility')])
def test_negative(fault,case):
    status,output=run(build('third_person',generated(fault)),case)
    assert status==CHECK_FAILED,output
