/* firefight.h: Firefight, waves of the campaign's enemies on a campaign
level (port/linux/game/firefight.c) */
#ifndef HALO_FIREFIGHT_H
#define HALO_FIREFIGHT_H

struct player_starting_location;

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

#endif
