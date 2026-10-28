"""Firefight (port/linux/game/firefight.c, network.firefight) in the game: each arena's level played alone with
Firefight on, its waves killed by console commands so that its rounds end, its players moved from stage to stage and
a second set begins; the lives running out to a game over and the main menu; the settings' lives, waves and starting
set; a level played without it; and a co-op game over system link (loopback), whose client follows the host's
director. Needs the retail maps in assets/maps and ninja linux's build/linux/halo; the game
runs without graphics or audio (debug.null_renderer), one tick a frame (debug.fixed_step), as fast as it can."""

import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

RETAIL_MAPS = Path("assets/maps")
GAME = Path("build/linux/halo")
# a player's line of the network test's log (network_test.c): its body and shield vitality, its deaths
PLAYER = re.compile(r"player (\d+): (?:\(-?[\d.]+ -?[\d.]+ -?[\d.]+\) h([\d.]+)/([\d.]+)|dead).*? d(-?\d+) ")


def _game(map_name):
    if not GAME.is_file() or not (RETAIL_MAPS / f"{map_name}.map").is_file():
        pytest.skip(f"needs {map_name}.map in assets/maps and ninja linux's build/linux/halo")


def _environment(folder, map_name, seconds, cues, settings):
    """a machine's folder (its data, saves and copy of the game) and environment"""
    (folder / "data").mkdir(parents=True)
    (folder / "save").mkdir()
    (folder / "data" / "maps").symlink_to(RETAIL_MAPS.resolve())
    if map_name:
        (folder / "data" / "init.txt").write_text(f"map_name levels\\{map_name}\\{map_name}\n")
    shutil.copy(GAME, folder / "halo")
    env = {**os.environ, "SDL_AUDIODRIVER": "dummy", "HALO_NULL_RENDERER": "true", "HALO_FIXED_STEP": "true",
           "HALO_DATA_ROOT": str(folder / "data"), "HALO_SAVE_ROOT": str(folder / "save"),
           "HALO_NO_AUDIO": "1", "HALO_UPDATE_AUTO": "false", "HALO_NET_ONLINE": "false",
           "HALO_NETWORK_TEST": "log", "HALO_NETWORK_TEST_CUES": "|".join(f"{second} {line}" for second, line in cues),
           "HALO_NET_FIREFIGHT": "on", "HALO_EXIT_AFTER": str(seconds)}
    env.update({name: str(value) for name, value in settings.items()})
    return env


def _play(tmp_path, map_name, seconds, cues, **settings):
    """the level played alone with Firefight for that many seconds, the console lines `cues` run at their game
    seconds: (exit status, its output, debug.txt)"""
    env = _environment(tmp_path, map_name, seconds, cues, settings)
    game = tmp_path / "halo"
    result = subprocess.run([str(game)], cwd=tmp_path, env=env, capture_output=True, text=True,
                            errors="replace", timeout=seconds + 120)
    debug = tmp_path / "data" / "debug.txt"
    return result.returncode, result.stdout + result.stderr, debug.read_text(errors="replace") if debug.is_file() else ""


def _kills(encounters, first=20, every=12, count=30):
    """console lines killing the arena's enemies every few seconds (each stage's encounter), and the player made
    deathless: the waves come and go without anyone fighting them"""
    line = "begin " + " ".join(f"(ai_kill {encounter})" for encounter in encounters)
    return [(1, "set cheat_deathless_player true")] + [(first + index * every, line) for index in range(count)]


def _director(output):
    return [line.split("firefight: ", 1)[1] for line in output.splitlines() if "firefight: " in line]


def _deaths(output):
    deaths = 0
    for match in PLAYER.finditer(output):
        deaths = max(deaths, int(match.group(4)))
    return deaths


def _clean(status, output, debug):
    assert status == 0, output[-2000:]
    assert not re.search(r"assert|exception", output + debug, re.I), "an assert or exception in the game's log"


@pytest.mark.parametrize("map_name,arena,encounters", [
    ("a30", "crash_site", ["first_wave", "rubble_wave", "river_wave", "cliff_wave"]),
    ("b40", "exterior", ["ext_a_area_a_cov", "ext_a_area_b_cov"]),
    ("c10", "swamp", ["swamp_b_flood"]),
])
def test_firefight_arena_plays_its_sets(tmp_path, map_name, arena, encounters):
    _game(map_name)
    status, output, debug = _play(tmp_path, map_name, 150, _kills(encounters))
    _clean(status, output, debug)
    director = _director(output)
    assert f"{arena} on {map_name}, {len(encounters)} stages, 7 lives (+1 a round), 5 waves a round, from set 1" in \
        director, director[:8]
    # (every stage found, with its groups and its players' starts)
    for encounter in encounters:
        stage = [line for line in director if line.startswith(f"stage {encounter},")]
        assert stage and not re.search(r" 0 groups| 0 player starts", stage[0]), stage
    assert not [line for line in director if " has no " in line], director
    # waves, a boss wave, rounds over with a life each, and every stage moved to
    assert "set 1 round 1 wave 5 (boss)" in " ".join(director)
    assert "set 1 round 1 over, 8 lives, team score 0" in director
    for encounter in encounters[1:]:
        assert f"moving to {encounter}" in director
    assert any(line.startswith("set 2 round 1 wave 1:") for line in director), director[-5:]
    # (the players, moved from stage to stage, never died)
    assert _deaths(output) == 0


def test_firefight_lives_run_out(tmp_path):
    """one life: the player killed respawns with it, killed again, the game is over, and back at the main menu"""
    _game("a30")
    kill = "unit_kill (unit (list_get (players) 0))"
    status, output, debug = _play(tmp_path, "a30", 60, [(15, kill), (30, kill)], HALO_NET_FIREFIGHT_LIVES=1)
    _clean(status, output, debug)
    director = _director(output)
    assert "player 0 respawned, 0 lives left" in director, director
    assert any(line.startswith("game over at set 1 round 1") for line in director), director
    assert "game over, ending the game" in director, director


def test_firefight_settings(tmp_path):
    """network.firefight_waves, _start_set, _lives and _round_lives as the director plays them"""
    _game("a30")
    status, output, debug = _play(tmp_path, "a30", 90, _kills(["first_wave", "rubble_wave"]),
                                  HALO_NET_FIREFIGHT_WAVES=3, HALO_NET_FIREFIGHT_START_SET=2,
                                  HALO_NET_FIREFIGHT_LIVES=3, HALO_NET_FIREFIGHT_ROUND_LIVES=2)
    _clean(status, output, debug)
    director = _director(output)
    assert "crash_site on a30, 4 stages, 3 lives (+2 a round), 3 waves a round, from set 2" in director, director[:5]
    assert any(line.startswith("set 2 round 1 wave 3 (boss):") for line in director), director
    assert not any(line.startswith("set 2 round 1 wave 4") for line in director), director
    assert "set 2 round 1 over, 5 lives, team score 0" in director, director


def test_firefight_off(tmp_path):
    """network.firefight off: the level played as the campaign has it, no director"""
    _game("a30")
    status, output, debug = _play(tmp_path, "a30", 30, [], HALO_NET_FIREFIGHT="off")
    _clean(status, output, debug)
    assert _director(output) == []


def test_firefight_coop_client_follows_the_host(tmp_path):
    """a30 hosted in co-op with Firefight, a client joined over loopback (its own network.firefight on, which a
    client ignores): the host's director plays, its waves killed so that a round ends and the players move to the
    next stage; the client runs no director, and logs the host's state as it changes, to the second round"""
    _game("a30")
    seconds = 170
    host = _environment(tmp_path / "host", None, seconds,
                        _kills(["first_wave", "rubble_wave"], first=15, every=10, count=20),
                        {"HALO_NETWORK_TEST": "host:levels\\a30\\a30:coop", "HALO_NETWORK_TEST_START": "15",
                         "HALO_NETWORK_TEST_CUE_PLAYERS": "2", "HALO_FIXED_STEP": "false",
                         "HALO_NET_ADDRESS": "127.0.0.210", "HALO_NET_BROADCAST": "127.0.0.211"})
    client = _environment(tmp_path / "client", None, seconds, [],
                          {"HALO_NETWORK_TEST": "join", "HALO_FIXED_STEP": "false",
                           "HALO_NET_ADDRESS": "127.0.0.211", "HALO_NET_BROADCAST": "127.0.0.210"})
    machines = [subprocess.Popen([str(tmp_path / name / "halo")], cwd=tmp_path / name, env=env,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
                for name, env in (("host", host), ("client", client))]
    outputs = [machine.communicate(timeout=seconds + 120)[0] for machine in machines]
    for machine, output in zip(machines, outputs):
        assert machine.returncode == 0, output[-2000:]
        assert not re.search(r"assert|exception", output, re.I), "an assert or exception in the game's log"
    host_director, client_director = _director(outputs[0]), _director(outputs[1])
    assert any(line.startswith("crash_site on a30, 4 stages") for line in host_director), host_director[:5]
    assert "set 1 round 1 over, 8 lives, team score 0" in host_director, host_director
    assert "moving to rubble_wave" in host_director, host_director
    # (the client: no director of its own, the host's state as it changes)
    assert not [line for line in client_director if not line.startswith("the host's state: ")], client_director
    assert "the host's state: set 1 round 1 wave 5, 7 lives, team score 0" in client_director, client_director
    assert any(line.startswith("the host's state: set 1 round 2 wave 1, 8 lives") for line in client_director), \
        client_director
