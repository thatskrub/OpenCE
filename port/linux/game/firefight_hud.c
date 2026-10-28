/*
FIREFIGHT_HUD.C

Firefight on the HUD (firefight.c): a status line at the top of each view
(the set, round and wave, the enemies left, the team's lives and the
viewer's score), and a banner in the middle for a few seconds when something
begins: a set, a round, a wave, the boss wave, a round's end (with its life)
and the game's end. Co-op's scoreboard (game_engine.c) is Firefight's too:
its title the set, round, lives and team score, a column of each player's
score, and shown without its button while the viewer is dead or the game is
over. A game alone has no scoreboard: its end is told in the middle of the
screen.

Every machine draws the same state: the director's on the host or a game
alone, the host's as it sends it on a co-op client (network_firefight.c).
*/

#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "input/input.h"
#include "interface/hud_messaging.h"
#include "memory/data.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"

#include "firefight.h"
#include "network_coop.h"
#include "network_firefight.h"

/* ---------- constants */

enum
{
	/* how long a banner shows */
	FIREFIGHT_BANNER_TICKS = 3 * TICKS_PER_SECOND,
	FIREFIGHT_BANNER_SIZE = 64,
};

/* ---------- globals */

/* the state the banners were last drawn for, and the banner showing */
static struct
{
	boolean known;
	struct network_firefight_status last;
	wchar_t banner[FIREFIGHT_BANNER_SIZE];
	long banner_time;
	/* the frame's local player the banner was checked for (once a frame,
	for the first view) */
	long checked_time;
} firefight_hud;

/* ---------- private code */

/* the HUD's text colour, lifted toward white (as the match clock's: hud.c) */
static void firefight_hud_color(
	real_argb_color *color)
{
	hud_get_text_color(color);
	color->red += (1.0f - color->red) * 0.65f;
	color->green += (1.0f - color->green) * 0.65f;
	color->blue += (1.0f - color->blue) * 0.65f;
	color->alpha = 1.0f;
}

/* centred text in the view, from `top` (a fraction of its height) */
static void firefight_hud_text(
	wchar_t const *text,
	real top,
	short lines)
{
	long font_index = hud_get_font_index();
	struct font_header *font;
	real_argb_color color;
	rectangle2d bounds;
	short height;

	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	height = (short)(font->ascending_height + font->descending_height);
	bounds = render.camera.window_bounds;
	bounds.y0 = (short)(bounds.y0 + (bounds.y1 - bounds.y0) * top);
	bounds.y1 = (short)(bounds.y0 + lines * height + 4);
	firefight_hud_color(&color);
	/* draw mode 2: centred */
	draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}

static long firefight_player_score(
	struct network_firefight_status const *status,
	long player_index)
{
	short index;

	for (index = 0; index < status->score_count; index++)
	{
		if (status->scores[index].player_index == DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index))
			return status->scores[index].score;
	}
	return 0;
}

static void firefight_set_banner(
	wchar_t const *text)
{
	ustrncpy(firefight_hud.banner, text, FIREFIGHT_BANNER_SIZE - 1);
	firefight_hud.banner[FIREFIGHT_BANNER_SIZE - 1] = 0;
	firefight_hud.banner_time = game_time_get();
}

/* a banner for what began since the state last seen */
static void firefight_check_banner(
	struct network_firefight_status const *status)
{
	struct network_firefight_status const *last = &firefight_hud.last;
	wchar_t text[FIREFIGHT_BANNER_SIZE];

	text[0] = 0;
	if (!firefight_hud.known)
		usnprintf(text, NUMBEROF(text) - 1, L"FIREFIGHT\r\nSET %d   ROUND %d", status->set + 1, status->round + 1);
	else if (status->game_over && !last->game_over)
		usnprintf(text, NUMBEROF(text) - 1, L"GAME OVER");
	else if (status->set != last->set)
		usnprintf(text, NUMBEROF(text) - 1, L"SET %d\r\nROUND %d", status->set + 1, status->round + 1);
	else if (status->round != last->round)
		usnprintf(text, NUMBEROF(text) - 1, L"ROUND %d", status->round + 1);
	else if (status->state == _firefight_state_intermission && last->state != _firefight_state_intermission)
		usnprintf(text, NUMBEROF(text) - 1, L"ROUND COMPLETE\r\n+1 LIFE");
	else if (status->wave != last->wave)
	{
		if (status->wave == FIREFIGHT_WAVES_PER_ROUND - 1)
			usnprintf(text, NUMBEROF(text) - 1, L"BOSS WAVE");
		else
			usnprintf(text, NUMBEROF(text) - 1, L"WAVE %d", status->wave + 1);
	}
	text[NUMBEROF(text) - 1] = 0;
	if (text[0])
		firefight_set_banner(text);
	firefight_hud.last = *status;
	firefight_hud.known = TRUE;
}

/* ---------- public code */

void firefight_hud_draw(
	short local_player_index)
{
	struct network_firefight_status status;
	long player_index = local_player_get_player_index(local_player_index);
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
	wchar_t text[128];

	if (!firefight_status(&status))
	{
		firefight_hud.known = FALSE;
		return;
	}
	if (firefight_hud.checked_time != game_time_get() || !firefight_hud.known)
	{
		firefight_check_banner(&status);
		firefight_hud.checked_time = game_time_get();
	}

	/* the status lines (none once the game is over: the banner and the
	scoreboard, or alone the lines below, tell it) */
	if (status.state == _firefight_state_game_over)
		text[0] = 0;
	else if (status.state == _firefight_state_intermission)
		usnprintf(text, NUMBEROF(text) - 1, L"SET %d   ROUND %d COMPLETE\r\nLIVES %d   SCORE %ld", status.set + 1,
			status.round + 1, status.lives, player ? firefight_player_score(&status, player_index) : 0);
	else
	{
		usnprintf(text, NUMBEROF(text) - 1, L"SET %d   ROUND %d   %s %d/%d\r\nENEMIES %d   LIVES %d   SCORE %ld",
			status.set + 1, status.round + 1,
			status.wave == FIREFIGHT_WAVES_PER_ROUND - 1 ? L"BOSS WAVE" : L"WAVE",
			status.wave + 1, FIREFIGHT_WAVES_PER_ROUND, status.living, status.lives,
			player ? firefight_player_score(&status, player_index) : 0);
	}
	text[NUMBEROF(text) - 1] = 0;
	/* (two short lines, between the ammunition's count and the shields) */
	if (text[0])
		firefight_hud_text(text, 0.02f, 2);

	/* the banner */
	if (firefight_hud.banner[0] &&
		(status.game_over || game_time_get() - firefight_hud.banner_time < FIREFIGHT_BANNER_TICKS))
	{
		firefight_hud_text(firefight_hud.banner, 0.28f, 2);
	}

	/* alone, dead: no spectating's line (coop_spectate.c), so this one; and
	the game's end, with no scoreboard */
	if (!network_coop_active() && player)
	{
		if (status.game_over)
		{
			usnprintf(text, NUMBEROF(text) - 1, L"SET %d   ROUND %d   WAVE %d\r\nTEAM SCORE %ld", status.set + 1,
				status.round + 1, status.wave + 1, status.team_score);
			text[NUMBEROF(text) - 1] = 0;
			firefight_hud_text(text, 0.45f, 2);
		}
		else if (player->unit_index == NONE)
		{
			firefight_hud_text(status.lives > 0 ? L"RESPAWNING" : L"NO LIVES LEFT", 0.45f, 1);
		}
	}
	/* in co-op, a dead player with no lives left to come back with */
	else if (player && player->unit_index == NONE && status.lives <= 0 && !status.game_over)
	{
		firefight_hud_text(L"NO LIVES LEFT: YOU COME BACK NEXT ROUND", 0.45f, 1);
	}
}

boolean firefight_scoreboard_title(
	wchar_t *title,
	short size)
{
	struct network_firefight_status status;

	if (!firefight_status(&status))
		return FALSE;
	usnprintf(title, size - 1, L"Firefight   Set %d   Round %d   Lives %d   Team %ld%s", status.set + 1,
		status.round + 1, status.lives, status.team_score, status.game_over ? L"   GAME OVER" : L"");
	title[size - 1] = 0;
	return TRUE;
}

boolean firefight_scoreboard_heading(
	wchar_t *heading,
	short size)
{
	struct network_firefight_status status;

	if (!firefight_status(&status))
		return FALSE;
	usnprintf(heading, size - 1, L"Score");
	heading[size - 1] = 0;
	return TRUE;
}

boolean firefight_scoreboard_score(
	long player_index,
	wchar_t *score,
	short size)
{
	struct network_firefight_status status;

	if (!firefight_status(&status))
		return FALSE;
	usnprintf(score, size - 1, L"%ld", firefight_player_score(&status, player_index));
	score[size - 1] = 0;
	return TRUE;
}

boolean firefight_scoreboard_shown(
	short local_player_index)
{
	struct network_firefight_status status;
	long player_index = local_player_get_player_index(local_player_index);

	if (!firefight_status(&status))
		return FALSE;
	return status.game_over || (player_index != NONE && player_get(player_index)->unit_index == NONE);
}
