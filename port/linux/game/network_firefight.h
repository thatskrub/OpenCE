/* network_firefight.h: the host's Firefight state, sent to its co-op
clients (port/linux/game/network_firefight.c) */
#ifndef NETWORK_FIREFIGHT_H
#define NETWORK_FIREFIGHT_H
#include "cseries.h"

/* the players' scores a state carries at most (co-op starts with 16) */
#define NETWORK_FIREFIGHT_MAXIMUM_SCORES 32

/* distributed_firefight_state.flags */
enum
{
	_network_firefight_active_bit,
	/* every player dead and no lives left: the game ends shortly */
	_network_firefight_game_over_bit,
	NUMBER_OF_NETWORK_FIREFIGHT_FLAGS
};

/* a player's score, by the player's absolute index (the same on every
machine of the game) */
struct distributed_firefight_score
{
	word player_index;
	word reserved;
	long score;
};

/* the director's state, as the host has it; count scores follow it */
struct distributed_firefight_state
{
	byte flags;
	/* the director's state and the stage played (firefight.c's) */
	byte state;
	byte stage;
	/* counting from 0 */
	byte wave;
	word set;
	word round;
	word lives;
	/* the stage's enemies alive */
	word living;
	long team_score;
	/* a round's waves, the last its boss wave */
	byte waves_per_round;
	byte reserved[3];
};

/* what the host's state says, for the HUD (step 6) */
struct network_firefight_status
{
	boolean active;
	boolean game_over;
	short state;
	short stage;
	short set;
	short round;
	short wave;
	short waves_per_round;
	short lives;
	short living;
	long team_score;
	short score_count;
	struct distributed_firefight_score scores[NETWORK_FIREFIGHT_MAXIMUM_SCORES];
};

/* a game begins: no state from a host yet */
void network_firefight_reset(void);
/* host, after its director's tick: the state to its clients, unreliably,
twice a second and when it changes */
void network_firefight_update(struct network_firefight_status const *status);
/* client: the host's latest state; FALSE if none has come */
boolean network_firefight_status(struct network_firefight_status *status);
boolean network_firefight_handles_message(word const *message, word size);
void network_firefight_handle_message(long machine_index, word const *message, word size);
#endif
