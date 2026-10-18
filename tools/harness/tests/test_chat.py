"""Upstream chat editor with the fork's local commands and token completion."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, function, structure, enum_with, constant, run, mutated, CHECK_FAILED


def generated(fault=None):
    source = read('port/linux/game/network_chat.c')
    names = ('chat_lower', 'chat_command_word', 'chat_command_parse', 'chat_player_name_ascii',
             'chat_prefix', 'chat_candidate', 'chat_complete', 'chat_input_begin', 'chat_input_end',
             'chat_input_submit', 'chat_input_insert', 'network_chat_update', 'network_chat_typing')
    code = '\n'.join(function(source, name) for name in names)
    if fault == 'slash':
        code = mutated(code, "if (text[0]=='/' && text[1]", 'if (FALSE && text[1]')
    if fault == 'case':
        code = mutated(code, 'chat_lower(*text++) != chat_lower(*prefix++)', '*text++ != *prefix++')
    if fault == 'cursor':
        code = mutated(code, 'chat_input.cursor=chat_completion.start+(short)n;', 'chat_input.cursor=(short)strlen(chat_input.text);')
    config = enum_with(read('source/input/input.h'), '_key_escape')+'\n'
    config += structure(read('source/input/input.h'), 'key_stroke')+'\n'
    config += enum_with(read('port/linux/game/network_chat.h'), '_chat_channel_everyone,')+'\n'
    config += enum_with(source, '_chat_command_none,')+'\n'
    for name in ('CHAT_TEXT_SIZE', 'CHAT_NAME_SIZE', 'CHAT_CANDIDATES', 'CHAT_KEY_MODIFIER_CONTROL_BIT',
                 'CHAT_MUTE_DEFAULT_MINUTES', 'CHAT_MUTE_MAXIMUM_MINUTES'):
        config += f'#define {name} {constant(source, name)}\n'
    config += re.search(r'static struct\s*\{[^}]+\} chat_input;', source).group()+'\n'
    config += re.search(r'static struct\s*\{[^}]+\} chat_completion;', source).group()+'\n'
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', ['ownership', 'send', 'commands', 'names', 'cycle', 'suffix', 'bounds',
                                 'channels', 'editing', 'paste', 'mute'])
def test_case(case):
    status, output = run(build('chat', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [('slash', 'send'), ('case', 'names'), ('cursor', 'suffix')])
def test_negative(fault, case):
    status, output = run(build('chat', generated(fault)), case)
    assert status == CHECK_FAILED, output
