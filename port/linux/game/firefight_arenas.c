/*
FIREFIGHT_ARENAS.C

Firefight's arenas (firefight.c): for each, the campaign level, and the
stages its players fight in, each an encounter of the level whose squads come
in waves. The names are the level's own (its scripts' ai_place and ai_migrate
name the same platoons), found when a game begins; a name the level lacks
leaves its group out.

Bungie built a30's (the level Halo) as the scripts' waves: each wave's squads are placed in a landing-zone
platoon (wave_N_lz_toon), which the scripts carry in a dropship and then
migrate to the wave's attacking platoon (wave_N_attack_toon), whose firing
positions come at the players. The other levels' encounters hold their
ground: their squads are sent to an empty squad of the encounter whose
firing positions are nearest the players' start (as their scripts
ai_migrate them), and Flood come at the players by themselves.
*/

#include "cseries.h"

#include "firefight_arenas.h"

#include <string.h>

/* ---------- a30: the crash site, then the rubble, the river and the cliff */

static struct firefight_group const a30_crash_site_groups[] =
{
	{ "wave_1_lz_toon", "wave_1_attack_toon", _firefight_pool_light },
	{ "wave_3_lz_toon", "wave_3_attack_toon", _firefight_pool_light },
	{ "wave_4_lz_toon", "wave_4_attack_toon", _firefight_pool_light },
	{ "wave_2_lz_toon", "wave_2_attack_toon", _firefight_pool_heavy },
	{ "wave_5_lz_toon", "wave_5_attack_toon", _firefight_pool_heavy },
	{ "wave_6_lz_toon", "wave_6_attack_toon", _firefight_pool_heavy },
};

static struct firefight_group const a30_rubble_groups[] =
{
	{ "wave_1_toon", NULL, _firefight_pool_light },
	{ "wave_2_lz_toon", "wave_2_attack_toon", _firefight_pool_light },
	{ "wave_5_lz_toon", "wave_5_attack_toon", _firefight_pool_light },
	{ "wave_3_lz_toon", "wave_3_attack_toon", _firefight_pool_heavy },
	{ "wave_4_lz_toon", "wave_4_attack_toon", _firefight_pool_heavy },
};

static struct firefight_group const a30_river_groups[] =
{
	{ "wave_2_lz_toon", "wave_2_attack_toon", _firefight_pool_light },
	{ "wave_1_toon", NULL, _firefight_pool_heavy },
};

/* (the cliff's are its forts' defenders, who hold them) */
static struct firefight_group const a30_cliff_groups[] =
{
	{ "main_near_toon", NULL, _firefight_pool_light },
	{ "main_far_toon", NULL, _firefight_pool_light },
	{ "right_fort_far_toon", NULL, _firefight_pool_light },
	{ "left_fort_far_toon", NULL, _firefight_pool_light },
	{ "inside_bottom", NULL, _firefight_pool_light },
	{ "main_top_toon", NULL, _firefight_pool_heavy },
	{ "right_fort_toon", NULL, _firefight_pool_heavy },
	{ "left_fort_toon", NULL, _firefight_pool_heavy },
};

static struct firefight_stage const a30_stages[] =
{
	{ "first_wave", NULL, a30_crash_site_groups, NUMBEROF(a30_crash_site_groups) },
	{ "rubble_wave", NULL, a30_rubble_groups, NUMBEROF(a30_rubble_groups) },
	/* (the river has no player starting location: where its Marines stand) */
	{ "river_wave", "river_marine", a30_river_groups, NUMBEROF(a30_river_groups) },
	{ "cliff_wave", NULL, a30_cliff_groups, NUMBEROF(a30_cliff_groups) },
};

/* (the Marines' shouts the level's wave scripts play: mission_first_wave_2's,
_3's and _4's) */
static char const *const a30_wave_sounds[] =
{
	"sound\\dialog\\a30\\a30_210_bisenti",
	"sound\\dialog\\a30\\a30_210_fitzgerald",
	"sound\\dialog\\a30\\a30_220_bisenti",
};

/* ---------- b40: the snowfield below the bridge, its two ends */

/* (the players at the Marines' start, beside the cliff; the squads, of two
or one, placed twice, come to squad_g's firing positions, 25 from them, the
Elites to squad_e's, 10 (squads left at their own, farther, may never
fire); grunts_g and jackals_g hold squad_e's already) */
static struct firefight_group const b40_snowfield_groups[] =
{
	{ "grunts_k", "squad_g", _firefight_pool_light, 2 },
	{ "jackals_k2", "squad_g", _firefight_pool_light, 2 },
	{ "grunts_a", "squad_g", _firefight_pool_light, 2 },
	{ "jackals_o", "squad_g", _firefight_pool_light, 2 },
	{ "jackals_q", "squad_g", _firefight_pool_light, 2 },
	{ "jackals_i", "squad_g", _firefight_pool_light, 2 },
	{ "grunts_g", NULL, _firefight_pool_light, 2 },
	{ "jackals_g", NULL, _firefight_pool_light, 2 },
	{ "elites_k", "squad_e", _firefight_pool_heavy, 2 },
	{ "jackals_k1", "squad_e", _firefight_pool_heavy, 2 },
	{ "elites_o", "squad_e", _firefight_pool_heavy, 2 },
	{ "elites_i", "squad_e", _firefight_pool_heavy, 2 },
	{ "elites_a", "squad_e", _firefight_pool_heavy, 2 },
};

/* (the snowfield's far side, where the Pelican's Marines land; the squads,
placed twice, come to squad_g's firing positions, 15 from them (from their
own, 20 to 40 away, they did not fire); not elites_e and its platoon, which
stand at the Marines'.
The valley beyond the tunnel was tried: its waves, up on its plateau, never
came down to its floor) */
static struct firefight_group const b40_landing_groups[] =
{
	{ "grunts_q", "squad_g", _firefight_pool_light, 2 },
	{ "jackals_q", "squad_g", _firefight_pool_light, 2 },
	{ "grunts_k", "squad_g", _firefight_pool_light, 2 },
	{ "jackals_k", "squad_g", _firefight_pool_light, 2 },
	{ "grunts_u", "squad_g", _firefight_pool_light, 2 },
	{ "jackals_u", "squad_g", _firefight_pool_light, 2 },
	{ "elites_q", "squad_g", _firefight_pool_heavy, 2 },
	{ "elites_k", "squad_g", _firefight_pool_heavy, 2 },
	{ "elites_u", "squad_g", _firefight_pool_heavy, 2 },
};

static struct firefight_stage const b40_stages[] =
{
	{ "ext_a_area_a_cov", "ext_a_area_a_marines", b40_snowfield_groups, NUMBEROF(b40_snowfield_groups),
		"marines_ini" },
	{ "ext_a_area_b_cov", "ext_a_area_b_marines", b40_landing_groups, NUMBEROF(b40_landing_groups),
		"marines_ini" },
};

/* ---------- c10: the swamp below the lift (its labs were tried: their Flood
stay on the walkways and in the rooms the scripts' doors and lifts join, and
never came at the players) */

/* (Flood combat forms: the swamp's squads are of one each, placed three
times over their starting locations) */
static struct firefight_group const c10_swamp_groups[] =
{
	{ "flood_a", NULL, _firefight_pool_light, 3 },
	{ "flood_b", NULL, _firefight_pool_light, 3 },
	{ "flood_c", NULL, _firefight_pool_light, 3 },
	{ "flood_j", NULL, _firefight_pool_light, 3 },
	{ "flood_k", NULL, _firefight_pool_light, 3 },
	{ "flood_l", NULL, _firefight_pool_light, 3 },
	{ "flood_m", NULL, _firefight_pool_light, 3 },
	{ "flood_n", NULL, _firefight_pool_light, 3 },
	{ "flood_initial", NULL, _firefight_pool_heavy },
	{ "flood_d", NULL, _firefight_pool_heavy, 4 },
	{ "flood_e", NULL, _firefight_pool_heavy, 4 },
	{ "last_wave", NULL, _firefight_pool_boss },
};

static struct firefight_stage const c10_stages[] =
{
	{ "swamp_b_flood", "swamp_b_marines", c10_swamp_groups, NUMBEROF(c10_swamp_groups), "lift_meat" },
};

/* ---------- the other levels' wave sound: no script of theirs announces a
wave, so the Marines' own shout when a fight begins (their combat dialog's
newcombatalone, which b40 and c10 load) */

static char const *const marine_wave_sounds[] =
{
	"sound\\dialog\\marines\\aussie\\conditional\\combat2\\shouting\\newcombatalone",
	"sound\\dialog\\marines\\bisenti\\conditional\\combat2\\shouting\\newcombatalone",
	"sound\\dialog\\marines\\fitzgerald\\conditional\\combat2\\shouting\\newcombatalone",
	"sound\\dialog\\marines\\mendoza\\conditional\\combat2\\shouting\\newcombatalone",
};

/* ---------- the arenas */

static struct firefight_arena const firefight_arenas[] =
{
	{ "crash_site", "a30", a30_stages, NUMBEROF(a30_stages), a30_wave_sounds, NUMBEROF(a30_wave_sounds) },
	{ "exterior", "b40", b40_stages, NUMBEROF(b40_stages), marine_wave_sounds, NUMBEROF(marine_wave_sounds) },
	{ "swamp", "c10", c10_stages, NUMBEROF(c10_stages), marine_wave_sounds, NUMBEROF(marine_wave_sounds) },
};

struct firefight_arena const *firefight_arena_get(
	char const *name)
{
	short index;

	for (index = 0; name && index < NUMBEROF(firefight_arenas); index++)
	{
		if (!_stricmp(firefight_arenas[index].name, name))
			return &firefight_arenas[index];
	}
	return NULL;
}

struct firefight_arena const *firefight_arena_for_scenario(
	char const *scenario)
{
	short index;

	for (index = 0; scenario && index < NUMBEROF(firefight_arenas); index++)
	{
		if (!_stricmp(firefight_arenas[index].scenario, scenario))
			return &firefight_arenas[index];
	}
	return NULL;
}
