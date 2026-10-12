"""The real rules of network_voice.c the host relays each voice by: who hears whom in the lobby and in each mode
(network.voice_mode), and the largest packet a quality (network.voice_*_kbps) allows."""

import sys
import re
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["lobby", "off", "team-proximity", "team-enemy-proximity", "team-global", "team-global-enemy-proximity",
         "packet-limit", "all-global", "settings"]


def test_menu_modes():
    import port_settings
    row = next(row for row in port_settings.TEAMPLAY_ROWS if row[0] == "voice_mode")
    table = read("port/linux/game/menu_functions.c").split('{ "voice_mode_spinner"', 1)[1].split('{ "voice_lobby_spinner"', 1)[0]
    values = re.findall(r'"([a-z_]+)"', table)
    count = int(re.search(r', _option_setting, 0, 0, (\d+)', table)[1])
    assert values == ["off", "team_proximity", "team_enemy_proximity", "team_global", "team_global_enemy_proximity", "all_global"]
    assert count == len(values) == len(row[2]) == len(row[3]) == 6
    assert row[2][-1] == "ALL"

# a fault in network_voice.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "all-only-team": (("case _voice_mode_all_global:\n\t\treturn _voice_route_global;",
                        "case _voice_mode_all_global:\n\t\treturn teammates ? _voice_route_global : _voice_route_none;"),
                       "voice_route", "all-global"),
    "all-spatialized": (("case _voice_mode_all_global:\n\t\treturn _voice_route_global;",
                         "case _voice_mode_all_global:\n\t\treturn _voice_route_proximity;"),
                        "voice_route", "all-global"),
    "legacy-clients-disabled": (("voice_host_settings.mode == _voice_mode_all_global ?\n\t\t_voice_mode_team_global : voice_host_settings.mode",
                                 "voice_host_settings.mode"), "voice_host_tell", "settings"),
    "lobby-ignores-setting": (("return lobby_voice ? _voice_route_global : _voice_route_none;",
                               "return _voice_route_global;"), "voice_route", "lobby"),
    "proximity-ignores-death": (("boolean near = both_alive && distance <= range;",
                                 "boolean near = distance <= range;"), "voice_route", "team-proximity"),
    "proximity-ignores-range": (("boolean near = both_alive && distance <= range;", "boolean near = both_alive;"),
                                "voice_route", "team-enemy-proximity"),
    "team-hears-enemies": (("return teammates ? _voice_route_global : _voice_route_none;",
                            "return _voice_route_global;"), "voice_route", "team-global"),
    "enemies-heard-anywhere": (("\t\treturn near ? _voice_route_proximity : _voice_route_none;\n\tdefault:",
                                "\t\treturn _voice_route_proximity;\n\tdefault:"),
                               "voice_route", "team-global-enemy-proximity"),
    "no-packet-cap": (("return MIN(limit, VOICE_MAXIMUM_PACKET);", "return limit * 4;"), "voice_packet_limit",
                      "packet-limit"),
}


def generated(fault=None, faulty_function=None):
    source = read("port/linux/game/network_voice.c")
    header = read("port/linux/game/network_voice.h")
    audio = read("port/linux/src/voice_audio.h")
    config = enum_with(header, "_voice_mode_off") + "\n" + enum_with(header, "_voice_route_none") + "\n"
    config += f"#define VOICE_FRAMES_PER_SECOND {constant(source, 'VOICE_FRAMES_PER_SECOND')}\n"
    config += f"#define VOICE_MAXIMUM_PACKET {constant(audio, 'VOICE_MAXIMUM_PACKET')}\n"
    config += structure(source, "distributed_voice_config") + "\n"
    text = ""
    for name in ("voice_route", "voice_packet_limit", "voice_mode_from_text", "voice_host_tell", "voice_play"):
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("voice_route", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("voice_route", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"network_voice.c with a fault ({control}) passed '{case}': the test cannot see it"
