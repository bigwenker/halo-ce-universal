"""Campaign camera eligibility, transitions and local-only free look."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, function, run, mutated, CHECK_FAILED, enum_with


def generated(fault=None):
    code=re.sub(r'^#include "[^"\n]+"\n','',read('port/linux/game/third_person.c'),flags=re.M)
    code+='\n'+function(read('source/camera/director.c'),'director_set_camera')
    code+='\n'+function(read('source/camera/director.c'),'director_choose_game_perspective')
    if fault=='vehicle':
        code=mutated(code,'local_player_index == 0 && camera_proc !=', 'FALSE && camera_proc !=')
    if fault=='multiplayer':
        code=mutated(code,'!game_engine_running()','TRUE')
    observer=read('source/camera/observer.c')
    postcheck=function(observer,'observer_postcheck')
    postcheck=postcheck[:postcheck.index('\n\t{\n\t\tstruct location location;')]+'\n}'
    pipeline='static real const sine_region_angle = 0.174f;\n'+'\n'.join(function(observer,n) for n in (
        'observer_up_from_forward','observer_collision_test_differential','observer_check_penetration'))+'\n'+postcheck
    if fault=='front':code=mutated(code,'forward.k = -forward.k;',';')
    if fault=='collision':pipeline=mutated(pipeline,'focus_distance != 0.f','FALSE')
    return (('under_test.inc',code),('observer_test.inc',pipeline),
            ('observer_flags.inc',enum_with(read('source/camera/observer.h'),'_observer_command_valid_bit')))



@pytest.mark.parametrize('case',['toggle','eligibility','scope','distance','camera','front-transform','front-collision','free-look','vehicle-transition','scripted-transition','secondary-view'])
def test_case(case):
    status,output=run(build('third_person',generated()),case)
    assert status==0,output


@pytest.mark.parametrize('fault,case',[('vehicle','scripted-transition'),('multiplayer','eligibility'),('front','front-transform'),('collision','front-collision')])
def test_negative(fault,case):
    status,output=run(build('third_person',generated(fault)),case)
    assert status==CHECK_FAILED,output
