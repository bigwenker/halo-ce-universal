"""Optional fork capability negotiation and production transport admission."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, enum_with, function, run, mutated, CHECK_FAILED

def generated(fault=None):
    code = re.sub(r'^#include "[^"\n]+"\n', '', read('port/linux/game/network_extensions.c'), flags=re.M)
    if fault == 'gate':
        code = mutated(code, 'if (!feature) return TRUE;', 'return TRUE;')
    transport = read('port/linux/game/network_distributed.c')
    parts = '\n'.join(function(transport, n) for n in (
        'distributed_fill_header', 'distributed_entry_size', 'distributed_send',
        'distributed_send_to_machine', 'distributed_send_to_machine_reliably'))
    header = read('port/linux/game/network_distributed.h')
    return (('messages.inc', enum_with(header, '_distributed_message_player_prediction')),
            ('api.inc', read('port/linux/game/network_extensions.h')),
            ('under_test.inc', code), ('transport.inc', parts))

@pytest.mark.parametrize('case', ['legacy', 'negotiation', 'partial', 'model_capability', 'admission', 'reset', 'transport'])
def test_case(case):
    status, output = run(build('network_extensions', generated()), case)
    assert status == 0, output

def test_negative():
    status, output = run(build('network_extensions', generated('gate')), 'legacy')
    assert status == CHECK_FAILED, output

def test_protocol_and_unique_ids():
    limits = read('port/linux/include/halo_port_limits.h')
    assert '#define HALO_PORT_NETWORK_VERSION 29' in limits
    # The production enum is compiled by every test: duplicate private/core IDs
    # would also duplicate the cases below and fail compilation.
    h = read('port/linux/game/network_distributed.h')
    for name, value in [('request', 83), ('offer', 84), ('sources', 85), ('progress', 86)]:
        assert f'_distributed_message_map_share_{name} = {value}' in h
