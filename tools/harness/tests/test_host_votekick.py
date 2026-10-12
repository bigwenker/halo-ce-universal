"""Local host authority versus remote voting, through the real kick/notice path."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, structure

CASES = ["host", "active-target", "other-vote", "invalid", "own-machine", "client", "remote", "notice"]


def generated(fault=None):
    vote = read("port/linux/game/network_votekick.c")
    server = read("source/networking/network_server_manager.c")
    distributed = read("port/linux/game/network_distributed.c")
    pieces = [(vote, "votekick_host"), (vote, "votekick_end"),
              (distributed, "distributed_send_notice"), (distributed, "network_distributed_kick"),
              (server, "network_game_server_kick_named_machine"),
              (server, "network_game_server_kick_machine_of_player"),
              (vote, "network_votekick_host_kick"), (vote, "network_votekick_request"),
              (vote, "network_votekick_handle_request")]
    code = "\n".join(function(source, name) for source, name in pieces)
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", structure(vote, "distributed_votekick_request")), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run_case(generated(), case)
    assert status == 0, output


def run_case(code, case):
    from harness import run
    return run(build("host_votekick", code), case)


@pytest.mark.parametrize("fault,case", [
    (("return network_votekick_host_kick(player_index, FALSE);",
      "votekick_host_request(NONE, player_index); return TRUE;"), "host"),
    (("if (votekick_host())", "if (TRUE)"), "client"),
    (("votekick.active && votekick.target_machine == machine_index", "FALSE"), "active-target"),
    (("!player || player->quit_out_of_game", "!player"), "invalid"),
    (("|| !from_stream", ""), "remote"),
    (("%s was kicked by host", "%s kicked by the host"), "notice"),
])
def test_negative_control(fault, case):
    status, output = run_case(generated(fault), case)
    assert status == CHECK_FAILED, output
