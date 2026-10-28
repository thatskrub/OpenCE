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
positions come at the players.
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

/* ---------- the arenas */

static struct firefight_arena const firefight_arenas[] =
{
	{ "crash_site", "a30", a30_stages, NUMBEROF(a30_stages), a30_wave_sounds, NUMBEROF(a30_wave_sounds) },
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
