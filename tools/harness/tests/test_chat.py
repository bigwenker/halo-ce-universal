"""The actual chat editor: opening, ownership, slash commands, name/command cycling."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, function, structure, enum_with, run, mutated, CHECK_FAILED


def generated(fault=None):
    code=read('port/linux/game/chat.c')
    code=code.replace(function(code,'chat_draw'),'')
    code=re.sub(r'^#include "[^"\n]+"\n','',code,flags=re.M)
    if fault=='slash':code=mutated(code,"if(text[0]=='/' && text[1])",'if(FALSE)')
    if fault=='case':code=mutated(code,'tolower((unsigned char)*text++) != tolower((unsigned char)*prefix++)','*text++ != *prefix++')
    if fault=='cursor':code=mutated(code,'chat_input.edit.selection_start_index=NONE;','chat_input.edit.selection_start_index=NONE; chat_input.edit.insertion_point_index=strlen(chat_input.result);')
    config=enum_with(read('source/input/input.h'),'_key_escape')+'\n'
    config+=structure(read('source/input/input.h'),'key_stroke')+'\n'
    config+=structure(read('source/dialogs/edit_text.h'),'edit_text')+'\n'
    config+=enum_with(read('source/interface/terminal.h'),'TERMINAL_PRINTF_MAXIMUM_LINE_LENGTH')+'\n'
    config+=structure(read('source/interface/terminal.h'),'terminal_gets_state')+'\n'
    return (('config.inc',config),('under_test.inc',code))


@pytest.mark.parametrize('case',['ownership','send','commands','names','cycle','suffix','bounds','scroll'])
def test_case(case):
    status,output=run(build('chat',generated()),case)
    assert status==0,output


@pytest.mark.parametrize('fault,case',[('slash','send'),('case','names'),('cursor','suffix')])
def test_negative(fault,case):
    status,output=run(build('chat',generated(fault)),case)
    assert status==CHECK_FAILED,output
