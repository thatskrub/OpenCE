/*
FIREFIGHT.C

Firefight: the campaign's enemies in waves on a campaign level, played alone
or in network co-op. network.firefight names the arena (firefight_arenas.c);
when its level is played as a campaign level, the level's own enemies are not
placed and its scripts do not run (game.c), and this director, run by the
host (or the one machine of a local game) once a tick, places the waves.

A wave places groups of the arena's encounter (platoons or squads, as the
scripts' ai_place does), from a pool by the wave's place in its round: light
groups first, heavy ones later, and a boss wave last, whose enemies are all
major. A group that has an attacking platoon joins it (ai_migrate) once
placed, as the scripts move the waves Bungie built to the players. The next
wave comes once few of the last remain; the round ends once the boss wave's
last enemy is dead, and the next round begins after a pause. An arena of
several stages (encounters some way apart, a30's) plays each round in the
next: once the pause is over, the players are moved there (the structure BSP
switched first if it differs). A set is a round in each stage, at least
three; each set brings bigger waves and more major upgrades.

The team shares its lives: it starts with seven and gets one for each round
it finishes. A player who dies respawns as in co-op, beside a teammate, while
there are lives, each respawn taking one (players.c); when every player is
dead, those who can are respawned at the stage's start after a moment, and
with no lives left the game is over: back to the main menu, or in co-op the
round ends and everyone goes back to the lobby. Each kill scores its player
by the enemy's kind, with a bonus for a headshot or a melee kill.

Only the host decides; the waves' actors reach co-op clients as any actor
does (network_actors.c), and the director's state (its set, round and wave,
the lives and the scores) as network_firefight.c sends it.
*/

#include "cseries.h"
#include "ai/actors.h"
#include "ai/ai.h"
#include "ai/ai_scenario_definitions.h"
#include "ai/ai_script.h"
#include "ai/encounters.h"
#include "ai/actor_types.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "interface/hud.h"
#include "main/main.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "networking/network_server_manager.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_files.h"
#include "units/bipeds.h"
#include "units/units.h"

#include "coop_enemies.h"
#include "firefight.h"
#include "firefight_arenas.h"
#include "network_firefight.h"

#include <stdio.h>
#include <string.h>

void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	FIREFIGHT_MAXIMUM_STAGES = 8,
	FIREFIGHT_MAXIMUM_GROUPS = 16,
	FIREFIGHT_MAXIMUM_PLAYER_STARTS = 16,
	/* an encounter's firing positions (the most the maps have is 512) */
	FIREFIGHT_MAXIMUM_FIRING_POSITIONS = 1024,

	FIREFIGHT_WAVES_PER_ROUND = 5,
	FIREFIGHT_MINIMUM_ROUNDS_PER_SET = 3,

	/* the first wave, after the players arrive */
	FIREFIGHT_FIRST_WAVE_TICKS = 5 * TICKS_PER_SECOND,
	/* between a wave's thinning and the next */
	FIREFIGHT_WAVE_GAP_TICKS = 3 * TICKS_PER_SECOND,
	/* the least time a wave lasts before the next may come */
	FIREFIGHT_WAVE_MINIMUM_TICKS = 10 * TICKS_PER_SECOND,
	/* after a round's last enemy dies, before the next round */
	FIREFIGHT_INTERMISSION_TICKS = 30 * TICKS_PER_SECOND,
	/* after a wave is placed, before its groups join their attacking
	platoons (the scripts let their dropships land first) */
	FIREFIGHT_ATTACK_DELAY_TICKS = 3 * TICKS_PER_SECOND,
	/* how often the waves are shown where the players are, so that none
	waits out of sight of them (the scripts' ai_magically_see_players) */
	FIREFIGHT_SIGHT_TICKS = 10 * TICKS_PER_SECOND,

	/* the next wave comes when this many remain, or a quarter of the last;
	as many as this left of a boss wave are shown on the players' HUD */
	FIREFIGHT_REMAINING_ENEMIES = 2,
	/* a boss wave's last enemies, once shown, are killed after this long
	(one stuck where no player can reach would never let the round end) */
	FIREFIGHT_REVEALED_KILL_TICKS = 120 * TICKS_PER_SECOND,
	/* room kept for actors other than the waves' (co-op's extra enemies'
	cap leaves the same) */
	FIREFIGHT_RESERVED_ACTORS = 64,
	/* an enemy placed farther than this from every one of its squad's
	firing positions is moved onto one (firefight_drop_group) */
	FIREFIGHT_DROP_DISTANCE = 15,
	/* the spots around a stage's start tried for a player moved there */
	FIREFIGHT_TELEPORT_ATTEMPTS = 8,
	/* a player farther than this from the stage's start is moved there */
	FIREFIGHT_ARRIVED_DISTANCE = 20,

	/* the team's lives at the start, and for each round it finishes */
	FIREFIGHT_STARTING_LIVES = 7,
	FIREFIGHT_ROUND_LIVES = 1,
	/* every player dead: the time before they respawn at the stage's start */
	FIREFIGHT_ALL_DEAD_TICKS = 5 * TICKS_PER_SECOND,
	/* the game over: the time before the game ends */
	FIREFIGHT_GAME_OVER_TICKS = 10 * TICKS_PER_SECOND,

	/* a kill's score by the enemy's kind, and its bonuses */
	FIREFIGHT_SCORE_GRUNT = 10,
	FIREFIGHT_SCORE_JACKAL = 15,
	FIREFIGHT_SCORE_ELITE = 30,
	FIREFIGHT_SCORE_MAJOR_ELITE = 10,
	FIREFIGHT_SCORE_HUNTER = 50,
	FIREFIGHT_SCORE_COMBAT_FORM = 15,
	FIREFIGHT_SCORE_CARRIER_FORM = 5,
	FIREFIGHT_SCORE_INFECTION_FORM = 1,
	FIREFIGHT_SCORE_SENTINEL = 20,
	FIREFIGHT_SCORE_HEADSHOT = 10,
	FIREFIGHT_SCORE_MELEE = 10,
};

/* the major upgrade chance each set adds */
#define FIREFIGHT_SET_MAJOR_UPGRADE 0.15f

enum
{
	_firefight_state_moving,
	_firefight_state_wave_gap,
	_firefight_state_wave,
	_firefight_state_intermission,
	_firefight_state_game_over,
};

/* ---------- structures */

struct firefight_player_start
{
	real_point3d position;
	real facing;
};

struct firefight_stage_state
{
	struct firefight_stage const *definition;
	/* the encounter's absolute index; its ai reference is the same */
	long encounter_index;
	short structure_bsp_index;
	short group_count;
	long place_references[FIREFIGHT_MAXIMUM_GROUPS];
	long attack_references[FIREFIGHT_MAXIMUM_GROUPS];
	short pools[FIREFIGHT_MAXIMUM_GROUPS];
	/* the groups that placed nobody (none at the game's difficulty), not
	chosen again */
	unsigned long empty_groups;
	short player_start_count;
	struct firefight_player_start player_starts[FIREFIGHT_MAXIMUM_PLAYER_STARTS];
};

/* ---------- globals */

static struct
{
	boolean active;
	struct firefight_arena const *arena;
	short stage_count;
	struct firefight_stage_state stages[FIREFIGHT_MAXIMUM_STAGES];

	short state;
	long state_time;
	short stage;
	short set;
	short round;
	short wave;
	/* the wave's enemies when placed */
	short wave_enemies;
	/* placing a boss wave: every enemy major */
	boolean placing_boss;
	/* the groups placed by the wave, which join their attacking platoons at
	attack_time (NONE: done) */
	short attack_count;
	short attack_groups[FIREFIGHT_MAXIMUM_GROUPS];
	long attack_time;
	/* moving: the structure BSP was asked for */
	boolean bsp_requested;
	/* the boss wave's last enemies shown on the HUD, and when (NONE: not
	yet) */
	long revealed_time;
	short revealed_count;
	long revealed_units[FIREFIGHT_REMAINING_ENEMIES];

	/* the team's lives left */
	short lives;
	/* when every player was found dead (NONE: someone lives) */
	long all_dead_time;
	/* the game was ended (game over) */
	boolean ended;
	/* each player's score, by absolute index, and the team's */
	long scores[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long team_score;
} firefight;

/* ---------- private code */

static long firefight_state_ticks(
	void)
{
	return game_time_get() - firefight.state_time;
}

static void firefight_set_state(
	short state)
{
	firefight.state = state;
	firefight.state_time = game_time_get();
}

static short firefight_rounds_per_set(
	void)
{
	return MAX(firefight.stage_count, FIREFIGHT_MINIMUM_ROUNDS_PER_SET);
}

/* the scenario's name, its tag's last part (levels\a30\a30's a30) */
static char const *firefight_scenario_name(
	void)
{
	char const *path;
	char const *separator;

	if (global_scenario_index == NONE || !(path = tag_get_name(global_scenario_index)))
		return "";
	separator = strrchr(path, '\\');
	return separator ? separator + 1 : path;
}

static void firefight_add_player_start(
	struct firefight_stage_state *stage,
	real_point3d const *position,
	real facing)
{
	if (stage->player_start_count < FIREFIGHT_MAXIMUM_PLAYER_STARTS)
	{
		stage->player_starts[stage->player_start_count].position = *position;
		stage->player_starts[stage->player_start_count].facing = facing;
		stage->player_start_count++;
	}
}

/* the players' starts: the encounter's player starting locations, else
its squads' starting locations */
static void firefight_find_player_starts(
	struct firefight_stage_state *stage,
	long encounter_index)
{
	struct scenario *scenario = global_scenario_get();
	struct encounter_definition *encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters, encounter_index,
		struct encounter_definition);
	short index;

	for (index = 0; index < encounter->player_starting_locations.count; index++)
	{
		struct player_starting_location *location = TAG_BLOCK_GET_ELEMENT(&encounter->player_starting_locations,
			index, struct player_starting_location);

		firefight_add_player_start(stage, &location->position, location->facing);
	}
	if (stage->player_start_count == 0)
	{
		short squad_index;

		for (squad_index = 0; squad_index < encounter->squads.count; squad_index++)
		{
			struct squad_definition *squad = TAG_BLOCK_GET_ELEMENT(&encounter->squads, squad_index,
				struct squad_definition);

			for (index = 0; index < squad->starting_locations.count; index++)
			{
				struct actor_starting_location *location = TAG_BLOCK_GET_ELEMENT(&squad->starting_locations,
					index, struct actor_starting_location);

				firefight_add_player_start(stage, &location->position, location->facing);
			}
		}
	}
}

/* a stage's encounter, groups and players' starts, by their names in the
level; FALSE if it has no encounter or groups */
static boolean firefight_resolve_stage(
	struct firefight_stage const *definition,
	struct firefight_stage_state *stage)
{
	struct scenario *scenario = global_scenario_get();
	struct encounter_definition *encounter;
	long player_encounter_index;
	short index;

	csmemset(stage, 0, sizeof(*stage));
	stage->definition = definition;
	stage->encounter_index = scenario_get_encounter_by_name(scenario, definition->encounter);
	if (stage->encounter_index == NONE)
	{
		platform_log("firefight: %s has no encounter %s", firefight_scenario_name(), definition->encounter);
		return FALSE;
	}
	encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters, stage->encounter_index, struct encounter_definition);
	stage->structure_bsp_index = encounter->runtime_structure_bsp_reference_index;
	if (!VALID_INDEX(stage->structure_bsp_index, scenario->structure_bsp_references.count))
		stage->structure_bsp_index = 0;

	for (index = 0; index < definition->group_count && stage->group_count < FIREFIGHT_MAXIMUM_GROUPS; index++)
	{
		struct firefight_group const *group = &definition->groups[index];
		char name[2 * (TAG_STRING_LENGTH + 1) + 1];
		long place_reference = NONE;
		long attack_reference = NONE;

		snprintf(name, sizeof(name), "%s/%s", definition->encounter, group->place);
		if (!ai_index_from_string(scenario, name, &place_reference) || place_reference == NONE)
		{
			platform_log("firefight: %s has no %s", firefight_scenario_name(), name);
			continue;
		}
		if (group->attack)
		{
			snprintf(name, sizeof(name), "%s/%s", definition->encounter, group->attack);
			if (!ai_index_from_string(scenario, name, &attack_reference))
				attack_reference = NONE;
		}
		stage->place_references[stage->group_count] = place_reference;
		stage->attack_references[stage->group_count] = attack_reference;
		stage->pools[stage->group_count] = group->pool;
		stage->group_count++;
	}

	player_encounter_index = definition->player_encounter ?
		scenario_get_encounter_by_name(scenario, definition->player_encounter) :
		stage->encounter_index;
	if (player_encounter_index != NONE)
		firefight_find_player_starts(stage, player_encounter_index);
	platform_log("firefight: stage %s, structure bsp %d, %d groups, %d player starts", definition->encounter,
		stage->structure_bsp_index, stage->group_count, stage->player_start_count);

	return stage->group_count > 0;
}

static struct firefight_stage_state *firefight_current_stage(
	void)
{
	return &firefight.stages[firefight.stage];
}

/* where the player starts in the stage: the stage's starts in turn, at a
free spot on the floor around it (a start can be in the level's scenery, as
a30's crash site's is in a rock, and the players share the starts). With
`taken` (COOP_ENEMIES_SPREAD_SPOTS bits), each call another spot. */
static boolean firefight_player_position(
	long player_index,
	long *taken,
	real_point3d *position,
	real *facing)
{
	struct firefight_stage_state *stage = firefight_current_stage();
	short number = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	struct firefight_player_start const *start;

	if (stage->player_start_count == 0)
		return FALSE;
	start = &stage->player_starts[number % stage->player_start_count];
	*position = start->position;
	*facing = start->facing;
	coop_enemies_spread_position(&start->position, (short)(number / stage->player_start_count + 1), taken,
		position);
	return TRUE;
}

static long firefight_living_count(
	void)
{
	return ai_scripting_living_count(firefight_current_stage()->encounter_index);
}

/* a group of the pool not yet placed in this wave (any, if none) */
static short firefight_choose_group(
	short pool,
	unsigned long placed)
{
	struct firefight_stage_state *stage = firefight_current_stage();
	short candidates[FIREFIGHT_MAXIMUM_GROUPS];
	short candidate_count = 0;
	short pass;
	short index;

	for (pass = 0; pass < 3 && candidate_count == 0; pass++)
	{
		for (index = 0; index < stage->group_count; index++)
		{
			boolean pool_matches = pass == 2 || stage->pools[index] == pool ||
				(pass == 1 && pool == _firefight_pool_boss && stage->pools[index] == _firefight_pool_heavy);

			if (pool_matches && !TEST_FLAG(stage->empty_groups, index) && (pass == 2 || !TEST_FLAG(placed, index)))
				candidates[candidate_count++] = index;
		}
	}
	if (candidate_count == 0)
		return NONE;
	return candidates[random_range(0, candidate_count)];
}

static boolean firefight_boss_wave(
	void)
{
	return firefight.wave == FIREFIGHT_WAVES_PER_ROUND - 1;
}

/* the scripts carried many of Bungie's waves in dropships, from where they
were placed (out of the way, out of reach) to their landing zone, the
firing positions they then hold. A group's enemies placed out of reach of
those are moved to one of them, as a dropship would have set them down, by
the teleport of the AI's command lists (action_obey.c). */
static void firefight_drop_group(
	long ai_reference)
{
	static short firing_position_indices[FIREFIGHT_MAXIMUM_FIRING_POSITIONS];
	struct scenario *scenario = global_scenario_get();
	struct ai_script_actor_reference_iterator iterator;
	struct actor_datum *actor;
	short number = 0;

	ai_index_actor_iterator_new(ai_reference, &iterator);
	while ((actor = ai_index_actor_iterator_next(&iterator)) != NULL)
	{
		struct encounter_definition *encounter;
		struct squad_definition *squad;
		struct object_datum *unit = object_try_and_get(actor->meta.unit_index);
		real nearest = FIREFIGHT_DROP_DISTANCE * FIREFIGHT_DROP_DISTANCE;
		short firing_position_count = 0;
		short index;

		if (!unit || actor->meta.encounter_index == NONE || actor->meta.squad_index == NONE)
			continue;
		encounter = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters,
			DATUM_INDEX_TO_ABSOLUTE_INDEX(actor->meta.encounter_index), struct encounter_definition);
		if (!VALID_INDEX(actor->meta.squad_index, encounter->squads.count))
			continue;
		squad = TAG_BLOCK_GET_ELEMENT(&encounter->squads, actor->meta.squad_index, struct squad_definition);
		for (index = 0;
			index < encounter->firing_positions.count && firing_position_count < NUMBEROF(firing_position_indices);
			index++)
		{
			struct firing_position_definition *firing_position = TAG_BLOCK_GET_ELEMENT(
				&encounter->firing_positions, index, struct firing_position_definition);

			if (VALID_INDEX(firing_position->group_index, MAXIMUM_NUMBER_OF_FIRING_POSITION_GROUPS * LONG_BITS) &&
				BIT_VECTOR_TEST_FLAG(squad->firing_position_groups, firing_position->group_index))
			{
				firing_position_indices[firing_position_count++] = index;
				nearest = MIN(nearest, distance_squared3d(&firing_position->position, &unit->object.position));
			}
		}
		if (firing_position_count > 0 && nearest >= FIREFIGHT_DROP_DISTANCE * FIREFIGHT_DROP_DISTANCE)
		{
			struct firing_position_definition *firing_position = TAG_BLOCK_GET_ELEMENT(&encounter->firing_positions,
				firing_position_indices[random_range(0, firing_position_count)], struct firing_position_definition);
			real_point3d position = firing_position->position;
			real_vector3d facing;

			coop_enemies_spread_position(&firing_position->position, ++number, NULL, &position);
			unit_get_facing_vector(actor->meta.unit_index, &facing);
			object_set_position(actor->meta.unit_index, &position, &facing, NULL);
			object_reset(actor->meta.unit_index);
			object_compute_node_matrices_recursive(actor->meta.unit_index);
			actor_input_sample_position(iterator.actor_index, actor->meta.unit_index, &actor->input.position);
			actor_move_halt(iterator.actor_index);
		}
	}
}

static void firefight_start_wave(
	void)
{
	struct firefight_stage_state *stage = firefight_current_stage();
	boolean boss = firefight_boss_wave();
	short group_count = (short)(1 + (firefight.wave >= 2 ? 1 : 0) + (boss ? 1 : 0) + firefight.set);
	long living_before = firefight_living_count();
	unsigned long placed = 0;
	short index = 0;
	short attempt;

	firefight.attack_count = 0;
	firefight.placing_boss = boss;
	for (attempt = 0; index < group_count && attempt < group_count + stage->group_count; attempt++)
	{
		short pool = boss ? _firefight_pool_boss :
			firefight.wave >= 2 && (index & 1) ? _firefight_pool_heavy : _firefight_pool_light;
		long living = firefight_living_count();
		short group;

		if (actor_data->actual_count >= MAXIMUM_ACTORS - FIREFIGHT_RESERVED_ACTORS)
			break;
		group = firefight_choose_group(pool, placed);
		if (group == NONE)
			break;
		SET_FLAG(placed, group, TRUE);
		ai_scripting_place(stage->place_references[group]);
		if (firefight_living_count() == living)
		{
			SET_FLAG(stage->empty_groups, group, TRUE);
			continue;
		}
		firefight_drop_group(stage->place_references[group]);
		if (stage->attack_references[group] != NONE && firefight.attack_count < FIREFIGHT_MAXIMUM_GROUPS)
			firefight.attack_groups[firefight.attack_count++] = group;
		index++;
	}
	firefight.placing_boss = FALSE;
	firefight.attack_time = firefight.attack_count > 0 ? game_time_get() + FIREFIGHT_ATTACK_DELAY_TICKS : NONE;
	firefight.wave_enemies = (short)MAX(firefight_living_count() - living_before, 0);
	platform_log("firefight: set %d round %d wave %d%s: %d enemies (%ld living)", firefight.set + 1,
		firefight.round + 1, firefight.wave + 1, boss ? " (boss)" : "", firefight.wave_enemies,
		firefight_living_count());
	firefight_set_state(_firefight_state_wave);
}

/* the wave's groups join their attacking platoons, and every enemy of the
stage is shown the players now and then: the waves come at them */
static void firefight_attack(
	void)
{
	struct firefight_stage_state *stage = firefight_current_stage();
	short index;

	if (firefight.attack_time != NONE && game_time_get() >= firefight.attack_time)
	{
		for (index = 0; index < firefight.attack_count; index++)
		{
			short group = firefight.attack_groups[index];

			ai_scripting_migrate(stage->place_references[group], stage->attack_references[group]);
		}
		firefight.attack_time = NONE;
		ai_scripting_magically_see_players(stage->encounter_index);
	}
	else if (firefight_state_ticks() % FIREFIGHT_SIGHT_TICKS == FIREFIGHT_SIGHT_TICKS - 1)
	{
		ai_scripting_magically_see_players(stage->encounter_index);
	}
}

/* the HUD's markers of the boss wave's last enemies taken down: those of
the dead (all, with `all`) */
static void firefight_hide_revealed(
	boolean all)
{
	short index = 0;

	while (index < firefight.revealed_count)
	{
		long unit_index = firefight.revealed_units[index];
		struct object_datum *unit = object_try_and_get(unit_index);

		if (all || !unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		{
			hud_deactivate_team_nav_point_with_object(_game_team_player, unit_index);
			firefight.revealed_units[index] = firefight.revealed_units[--firefight.revealed_count];
		}
		else
		{
			index++;
		}
	}
}

/* a boss wave's last enemies: shown on every player's HUD (as the scripts'
activate_team_nav_point_object, which co-op sends its clients), then, if
still alive two minutes later, killed */
static void firefight_stragglers(
	long living)
{
	struct ai_script_actor_reference_iterator iterator;
	struct actor_datum *actor;
	short nav_index;

	firefight_hide_revealed(FALSE);
	if (firefight.revealed_time == NONE)
	{
		if (living == 0 || living > FIREFIGHT_REMAINING_ENEMIES)
			return;
		nav_index = find_nav_point("default_red");
		if (nav_index == NONE)
			nav_index = find_nav_point("default");
		ai_index_actor_iterator_new(firefight_current_stage()->encounter_index, &iterator);
		while ((actor = ai_index_actor_iterator_next(&iterator)) != NULL &&
			firefight.revealed_count < FIREFIGHT_REMAINING_ENEMIES)
		{
			if (nav_index == NONE || actor->meta.unit_index == NONE)
				continue;
			hud_activate_team_nav_point_with_object(nav_index, _game_team_player, actor->meta.unit_index, 0.0f);
			firefight.revealed_units[firefight.revealed_count++] = actor->meta.unit_index;
		}
		firefight.revealed_time = game_time_get();
		platform_log("firefight: %ld last enemies shown", living);
	}
	else if (living > 0 && game_time_get() - firefight.revealed_time >= FIREFIGHT_REVEALED_KILL_TICKS)
	{
		platform_log("firefight: %ld last enemies killed", living);
		ai_scripting_kill(firefight_current_stage()->encounter_index);
	}
}

/* a place a biped stands, on the floor at or under `position`: a level's
starting locations are on their floor, some a little into it */
static boolean firefight_floor_position(
	real_point3d *position)
{
	struct collision_result collision;
	real_point3d from = *position;
	real_vector3d down = { 0.0f, 0.0f, -3.0f };

	from.z += 1.5f;
	if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&from, &down, NONE, &collision))
	{
		return FALSE;
	}
	*position = collision.point;
	position->z += 0.05f;
	return scenario_leaf_index_from_point(position) != NONE;
}

/* moving to the stage: its structure BSP loaded, then every player there */
static boolean firefight_move_players(
	void)
{
	struct firefight_stage_state *stage = firefight_current_stage();
	struct data_iterator iterator;
	struct player_datum *player;

	if (global_structure_bsp_index_get() != stage->structure_bsp_index)
	{
		if (!firefight.bsp_requested)
		{
			platform_log("firefight: switching to structure bsp %d", stage->structure_bsp_index);
			main_switch_structure_bsp(stage->structure_bsp_index);
			firefight.bsp_requested = TRUE;
		}
		return FALSE;
	}
	firefight.bsp_requested = FALSE;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum *unit = object_try_and_get(player->unit_index);
		real_point3d position;
		real facing;

		long taken[BIT_VECTOR_SIZE_IN_LONGS(COOP_ENEMIES_SPREAD_SPOTS)];
		short attempt;

		if (!unit || !firefight_player_position(iterator.datum_index, NULL, &position, &facing) ||
			distance_squared3d(&unit->object.position, &position) <=
				FIREFIGHT_ARRIVED_DISTANCE * FIREFIGHT_ARRIVED_DISTANCE)
		{
			continue;
		}
		/* (a failed teleport leaves the player dead: only to a place the
		biped is found to fit, the free spots around the start and last the
		start itself, else the player stays) */
		csmemset(taken, 0, sizeof(taken));
		for (attempt = 0; attempt < FIREFIGHT_TELEPORT_ATTEMPTS; attempt++)
		{
			real_point3d fitted;

			if (attempt < FIREFIGHT_TELEPORT_ATTEMPTS - 1)
				firefight_player_position(iterator.datum_index, taken, &position, &facing);
			else
				position = stage->player_starts[DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) %
					stage->player_start_count].position;
			if (firefight_floor_position(&position) &&
				biped_fix_position(player->unit_index, NONE, &position, &fitted, 2.0f, FALSE, TRUE, TRUE))
			{
				player_teleport(iterator.datum_index, NONE, &fitted);
				break;
			}
		}
	}
	return TRUE;
}

static void firefight_next_round(
	void)
{
	short next_stage = (short)((firefight.stage + 1) % firefight.stage_count);

	firefight.round++;
	if (firefight.round >= firefight_rounds_per_set())
	{
		firefight.round = 0;
		firefight.set++;
	}
	firefight.wave = 0;
	if (next_stage != firefight.stage)
	{
		ai_scripting_force_active(firefight_current_stage()->encounter_index, FALSE);
		firefight.stage = next_stage;
		platform_log("firefight: moving to %s", firefight_current_stage()->definition->encounter);
		firefight_set_state(_firefight_state_moving);
	}
	else
	{
		firefight_set_state(_firefight_state_wave_gap);
	}
}

/* a kill's score: by the enemy's kind (a major Elite's more), nothing for
anyone else */
static long firefight_kill_score(
	long unit_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index,
		_object_mask_unit);
	long actor_index;
	struct actor_datum *actor;
	char const *variant_name;

	if (!unit || !game_team_is_enemy(_game_team_player, unit->object.owner_team_index))
		return 0;
	actor_index = unit->unit.actor_index != NONE ? unit->unit.actor_index : unit->unit.swarm_actor_index;
	if (actor_index == NONE || !(actor = (struct actor_datum *)datum_try_and_get(actor_data, actor_index)))
		return 0;
	switch (actor->meta.type)
	{
	case _actor_grunt:
		return FIREFIGHT_SCORE_GRUNT;
	case _actor_jackal:
		return FIREFIGHT_SCORE_JACKAL;
	case _actor_elite:
		/* (the major variants are named so: "elite major plasma rifle") */
		variant_name = actor->meta.variant_definition_index != NONE ?
			tag_get_name(actor->meta.variant_definition_index) : NULL;
		return FIREFIGHT_SCORE_ELITE + (variant_name && strstr(variant_name, "major") ? FIREFIGHT_SCORE_MAJOR_ELITE : 0);
	case _actor_hunter:
		return FIREFIGHT_SCORE_HUNTER;
	case _actor_combat_form:
		return FIREFIGHT_SCORE_COMBAT_FORM;
	case _actor_carrier_form:
		return FIREFIGHT_SCORE_CARRIER_FORM;
	case _actor_infection_form:
		return FIREFIGHT_SCORE_INFECTION_FORM;
	case _actor_sentinel:
		return FIREFIGHT_SCORE_SENTINEL;
	default:
		return 0;
	}
}

static void firefight_log_scores(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		platform_log("firefight: player %d score %ld, deaths %d", (int)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index),
			(long)firefight.scores[DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index)], (int)player->statistics.deaths);
	}
	platform_log("firefight: team score %ld", firefight.team_score);
}

/* every player dead: after a moment, those the lives allow respawn at the
stage's start; with none left, the game is over. Once over, it ends: back to
the main menu, or a co-op host's round ends for everyone (as a won co-op
level's does: main.c), back to the lobby. */
static void firefight_update_team(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (firefight.state == _firefight_state_game_over)
	{
		if (!firefight.ended && firefight_state_ticks() >= FIREFIGHT_GAME_OVER_TICKS)
		{
			firefight.ended = TRUE;
			platform_log("firefight: game over, ending the game");
			if (game_connection() == _game_connection_network_server)
				network_game_server_port_cooperative_won(NULL);
			else
				main_goto_main_menu();
		}
		return;
	}
	if (!players_are_all_dead())
	{
		firefight.all_dead_time = NONE;
		return;
	}
	if (firefight.all_dead_time == NONE)
		firefight.all_dead_time = game_time_get();
	if (firefight.lives <= 0)
	{
		platform_log("firefight: game over at set %d round %d wave %d", firefight.set + 1, firefight.round + 1,
			firefight.wave + 1);
		firefight_log_scores();
		firefight_set_state(_firefight_state_game_over);
		return;
	}
	if (game_time_get() - firefight.all_dead_time < FIREFIGHT_ALL_DEAD_TICKS)
		return;
	firefight.all_dead_time = NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL && firefight.lives > 0)
	{
		if (player->unit_index == NONE && !player->quit_out_of_game)
			players_firefight_respawn(iterator.datum_index);
	}
}

/* the state the host's clients are sent (network_firefight.c) */
static void firefight_send_status(
	void)
{
	struct network_firefight_status status;
	struct data_iterator iterator;
	struct player_datum *player;

	csmemset(&status, 0, sizeof(status));
	status.active = TRUE;
	status.game_over = firefight.state == _firefight_state_game_over;
	status.state = firefight.state;
	status.stage = firefight.stage;
	status.set = firefight.set;
	status.round = firefight.round;
	status.wave = firefight.wave;
	status.lives = firefight.lives;
	status.living = (short)MIN(firefight_living_count(), SHORT_MAX);
	status.team_score = firefight.team_score;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
		status.score_count < NETWORK_FIREFIGHT_MAXIMUM_SCORES)
	{
		short index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);

		status.scores[status.score_count].player_index = (word)index;
		status.scores[status.score_count].score = firefight.scores[index];
		status.score_count++;
	}
	network_firefight_update(&status);
}

/* ---------- public code */

boolean firefight_active(
	void)
{
	struct network_firefight_status status;

	if (network_game_distributed_client())
		return network_firefight_status(&status) && status.active;
	return firefight.active;
}

boolean firefight_new_game(
	void)
{
	char const *arena_name = config_string("network.firefight");
	short index;

	csmemset(&firefight, 0, sizeof(firefight));
	/* (a co-op client's game is Firefight as its host's state says) */
	if (network_game_distributed_client())
		return FALSE;
	if (!arena_name || !arena_name[0] || game_in_editor() || !global_scenario ||
		global_scenario->type != _scenario_type_solo || game_engine_running())
	{
		return FALSE;
	}
	firefight.arena = firefight_arena_get(arena_name);
	if (!firefight.arena)
	{
		platform_log("firefight: no arena %s", arena_name);
		return FALSE;
	}
	if (_stricmp(firefight.arena->scenario, firefight_scenario_name()))
		return FALSE;

	for (index = 0; index < firefight.arena->stage_count && firefight.stage_count < FIREFIGHT_MAXIMUM_STAGES; index++)
	{
		if (firefight_resolve_stage(&firefight.arena->stages[index], &firefight.stages[firefight.stage_count]))
			firefight.stage_count++;
	}
	if (firefight.stage_count == 0)
	{
		platform_log("firefight: %s has no stage on %s", firefight.arena->name, firefight_scenario_name());
		return FALSE;
	}

	firefight.active = TRUE;
	firefight.attack_time = NONE;
	firefight.revealed_time = NONE;
	firefight.lives = FIREFIGHT_STARTING_LIVES;
	firefight.all_dead_time = NONE;
	firefight_set_state(_firefight_state_moving);
	platform_log("firefight: %s on %s, %d stages", firefight.arena->name, firefight_scenario_name(),
		firefight.stage_count);

	return TRUE;
}

void firefight_update(
	void)
{
	if (!firefight.active || network_game_distributed_client())
		return;

	switch (firefight.state)
	{
	case _firefight_state_moving:
		if (firefight_move_players())
		{
			/* the stage's enemies are active wherever they are, the players
			seeing them or not, as the scripts' ai_force_active keeps them */
			ai_scripting_force_active(firefight_current_stage()->encounter_index, TRUE);
			firefight_set_state(_firefight_state_wave_gap);
		}
		break;

	case _firefight_state_wave_gap:
		if (firefight_state_ticks() >=
			(firefight.wave == 0 ? FIREFIGHT_FIRST_WAVE_TICKS : FIREFIGHT_WAVE_GAP_TICKS))
		{
			firefight_start_wave();
		}
		break;

	case _firefight_state_wave:
	{
		long living = firefight_living_count();

		firefight_attack();
		if (firefight_boss_wave())
		{
			firefight_stragglers(living);
			if (living == 0)
			{
				firefight_hide_revealed(TRUE);
				firefight.revealed_time = NONE;
				firefight.lives = (short)MIN(firefight.lives + FIREFIGHT_ROUND_LIVES, SHORT_MAX);
				platform_log("firefight: set %d round %d over, %d lives, team score %ld", firefight.set + 1,
					firefight.round + 1, firefight.lives, firefight.team_score);
				firefight_set_state(_firefight_state_intermission);
			}
		}
		else if (firefight_state_ticks() >= FIREFIGHT_WAVE_MINIMUM_TICKS &&
			living <= MAX(FIREFIGHT_REMAINING_ENEMIES, firefight.wave_enemies / 4))
		{
			firefight.wave++;
			firefight_set_state(_firefight_state_wave_gap);
		}
		break;
	}

	case _firefight_state_intermission:
		if (firefight_state_ticks() >= FIREFIGHT_INTERMISSION_TICKS)
			firefight_next_round();
		break;

	case _firefight_state_game_over:
		break;
	}
	firefight_update_team();
	firefight_send_status();
}

boolean firefight_respawn_allowed(
	void)
{
	if (!firefight.active)
		return TRUE;
	return firefight.lives > 0 && firefight.state != _firefight_state_game_over;
}

void firefight_note_respawn(
	long player_index)
{
	if (!firefight.active || network_game_distributed_client())
		return;
	firefight.lives = (short)MAX(firefight.lives - 1, 0);
	platform_log("firefight: player %d respawned, %d lives left", (int)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index),
		(int)firefight.lives);
}

void firefight_note_kill(
	long unit_index,
	long player_index,
	boolean melee,
	boolean headshot)
{
	long score;
	short index;

	if (!firefight.active || network_game_distributed_client() || player_index == NONE ||
		!player_try_and_get(player_index))
	{
		return;
	}
	score = firefight_kill_score(unit_index);
	if (score <= 0)
		return;
	if (headshot)
		score += FIREFIGHT_SCORE_HEADSHOT;
	if (melee)
		score += FIREFIGHT_SCORE_MELEE;
	index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (VALID_INDEX(index, HALO_PORT_MAXIMUM_NETWORK_PLAYERS))
		firefight.scores[index] += score;
	firefight.team_score += score;
}

struct player_starting_location const *firefight_spawn_location(
	long player_index,
	struct player_starting_location *location)
{
	if (!firefight.active)
		return NULL;
	csmemset(location, 0, sizeof(*location));
	if (!firefight_player_position(player_index, NULL, &location->position, &location->facing))
		return NULL;
	location->structure_bsp_reference_index = firefight_current_stage()->structure_bsp_index;
	return location;
}

void firefight_major_upgrade_chance(
	boolean *force_major,
	boolean *is_random,
	real *random_chance)
{
	if (!firefight.active)
		return;
	if (firefight.placing_boss)
	{
		*force_major = TRUE;
		*is_random = FALSE;
	}
	else if (*is_random)
	{
		*random_chance = MIN(*random_chance + FIREFIGHT_SET_MAJOR_UPGRADE * firefight.set, 1.0f);
	}
}
