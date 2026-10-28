/* firefight_arenas.h: Firefight's arenas, the campaign levels' encounters
it plays (port/linux/game/firefight_arenas.c) */
#ifndef HALO_FIREFIGHT_ARENAS_H
#define HALO_FIREFIGHT_ARENAS_H

/* a group's pool: what kind of wave places it */
enum
{
	_firefight_pool_light,
	_firefight_pool_heavy,
	_firefight_pool_boss,
	_firefight_pool_vehicle,
	_firefight_pool_flood,
	NUMBER_OF_FIREFIGHT_POOLS
};

/* a group of enemies a wave places: a platoon or squad of the stage's
encounter, by name (as the scripts' ai_place names it), and the one it then
joins to come at the players (its firing positions), NULL to stay */
struct firefight_group
{
	char const *place;
	char const *attack;
	short pool;
};

/* a place an arena's players fight in: an encounter's waves, the players
starting at its player starting locations (else at those of the starting
locations of player_encounter's squads, where the level's Marines stand) */
struct firefight_stage
{
	char const *encounter;
	char const *player_encounter;
	struct firefight_group const *groups;
	short group_count;
};

/* an arena: a level and its stages, played in turn, a round each */
struct firefight_arena
{
	char const *name;
	/* the level's scenario name (levels\a30\a30's a30) */
	char const *scenario;
	struct firefight_stage const *stages;
	short stage_count;
};

/* the arena of that name, NULL if none */
struct firefight_arena const *firefight_arena_get(char const *name);

#endif
