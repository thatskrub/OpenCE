/* firefight.h: Firefight, waves of the campaign's enemies on a campaign
level (port/linux/game/firefight.c) */
#ifndef HALO_FIREFIGHT_H
#define HALO_FIREFIGHT_H

/* the director's states (network_firefight_status.state) */
enum
{
	_firefight_state_moving,
	_firefight_state_wave_gap,
	_firefight_state_wave,
	_firefight_state_intermission,
	_firefight_state_game_over,
};

struct player_starting_location;
struct network_firefight_status;

/* the game being played is Firefight: network.firefight names an arena of
the level loaded, played as a campaign level (alone or in co-op); on a co-op
client, the host's state says so (network_firefight.c) */
boolean firefight_active(void);
/* game.c: a game begins on a level; TRUE if it is Firefight, whose waves
take the place of the level's own enemies (ai_place) */
boolean firefight_new_game(void);
/* game.c: each tick, in place of the level's scripts (hs_update); only the
host's or a local game's director decides */
void firefight_update(void);
/* players.c's player_spawn: where a player starts, in the arena's current
stage; NULL outside Firefight */
struct player_starting_location const *firefight_spawn_location(long player_index,
	struct player_starting_location *location);
/* the game's Firefight state (the host's, on a co-op client), for the HUD;
FALSE outside Firefight */
boolean firefight_status(struct network_firefight_status *status);
/* players.c: whether a dead player may respawn (outside Firefight, always;
in it, while the team has lives) */
boolean firefight_respawn_allowed(void);
/* players.c's player_spawn: a player who had died spawned, taking a life */
void firefight_note_respawn(long player_index);
/* damage.c: a unit was killed by the player's damage (NONE: no player's),
a melee's, or a headshot's (killed at once) */
void firefight_note_kill(long unit_index, long player_index, boolean melee, boolean headshot);
/* ai.c's ai_get_major_upgrade_chance: the chance raised by the sets played,
and every enemy upgraded in a boss wave */
void firefight_major_upgrade_chance(boolean *force_major, boolean *is_random, real *random_chance);

/* ---------- firefight_hud.c */

/* hud.c: the status line and the banners, in a Firefight game */
void firefight_hud_draw(short local_player_index);
/* game_engine.c's scoreboard (co-op's): its title, its score column's
heading and a player's score; FALSE outside Firefight */
boolean firefight_scoreboard_title(wchar_t *title, short size);
boolean firefight_scoreboard_heading(wchar_t *heading, short size);
boolean firefight_scoreboard_score(long player_index, wchar_t *score, short size);
/* ... and whether it is shown without its button held: the viewer dead,
or the game over */
boolean firefight_scoreboard_shown(short local_player_index);

#endif
