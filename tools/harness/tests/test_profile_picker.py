"""Mouse/touch HSV drags use the production pointer handler."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,function,run,mutated,CHECK_FAILED
def generated(fault=False):
    source=read('port/linux/game/player_profile_color_menu.c')
    code=function(source,'inside')+'\n'+function(source,'profile_color_menu_pointer')
    code+='\n'+'\n'.join(function(source,name) for name in ('profile_color_menu_begin','profile_color_menu_reset','profile_color_menu_is_reset','profile_color_menu_adjust'))
    code=function(read('port/linux/game/player_profile_rgb.c'),'profile_rgb_to_hsv')+'\n'+code
    code+='\n'+function(read('port/linux/game/menu_functions.c'),'profile_edit_choose')
    code+='\n'+'\n'.join(function(read('port/linux/game/menu_functions.c'),name) for name in ('profile_edit_color_save','profile_edit_color_reset'))
    if fault=='history':code=mutated(code,'while (from->focused_child) from=from->focused_child;','/* lose the focused row */')
    elif fault:code=mutated(code,'!pointer->held','TRUE')
    return (('pointer.inc',read('port/linux/include/halo_ui_pointer.h')),('under_test.inc',code))
@pytest.mark.parametrize('case',['drag','touch','hue','inactive','reset','save-reset','reopen','model-default','save-failure','navigation'])
def test_case(case):
    status,out=run(build('profile_picker',generated()),case)
    assert status==0,out
def test_negative_release():
    status,out=run(build('profile_picker',generated(True)),'drag')
    assert status==CHECK_FAILED,out
def test_negative_history():
    status,out=run(build('profile_picker',generated('history')),'navigation')
    assert status==CHECK_FAILED,out
