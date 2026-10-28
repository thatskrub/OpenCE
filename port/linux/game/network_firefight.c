/* The host's Firefight state, for its co-op clients (firefight.c): which
set, round and wave it is, the lives the team has left, the enemies alive and
every player's score. Only the host's director decides; the host sends its
state to every client unreliably twice a second and as soon as it changes, and
a client keeps the latest whole message it checks (sizes, counts, indices),
dropping one older than the last. A client's own network.firefight is not
read: the host's state alone says the game is Firefight. */
#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "memory/data.h"
#include "networking/network_game_globals.h"
#include "network_distributed.h"
#include "network_firefight.h"
#include <string.h>

void platform_log(char const *format, ...);

/* how often the state goes out unchanged */
#define NETWORK_FIREFIGHT_SEND_TICKS (TICKS_PER_SECOND / 2)

struct distributed_firefight_state_message
{
	struct distributed_message_header header;
	struct distributed_firefight_state state;
	struct distributed_firefight_score scores[NETWORK_FIREFIGHT_MAXIMUM_SCORES];
};

static struct
{
	/* client: the host's latest, and its time (NONE: none yet) */
	long received_time;
	struct network_firefight_status received;
	/* host: the last sent, and when */
	long sent_time;
	struct distributed_firefight_state_message sent;
	word sent_size;
} network_firefight;

void network_firefight_reset(void)
{
	memset(&network_firefight, 0, sizeof(network_firefight));
	network_firefight.received_time = NONE;
	network_firefight.sent_time = NONE;
}

static word firefight_state_message_size(short score_count)
{
	return (word)(sizeof(struct distributed_firefight_state_message) -
		(NETWORK_FIREFIGHT_MAXIMUM_SCORES - score_count) * sizeof(struct distributed_firefight_score));
}

void network_firefight_update(struct network_firefight_status const *status)
{
	struct distributed_firefight_state_message message;
	short count = (short)PIN(status->score_count, 0, NETWORK_FIREFIGHT_MAXIMUM_SCORES);
	word size = firefight_state_message_size(count);

	if (game_connection() != _game_connection_network_server)
		return;
	memset(&message, 0, sizeof(message));
	SET_FLAG(message.state.flags, _network_firefight_active_bit, status->active);
	SET_FLAG(message.state.flags, _network_firefight_game_over_bit, status->game_over);
	message.state.state = (byte)PIN(status->state, 0, UNSIGNED_CHAR_MAX);
	message.state.stage = (byte)PIN(status->stage, 0, UNSIGNED_CHAR_MAX);
	message.state.wave = (byte)PIN(status->wave, 0, UNSIGNED_CHAR_MAX);
	message.state.waves_per_round = (byte)PIN(status->waves_per_round, 1, UNSIGNED_CHAR_MAX);
	message.state.set = (word)PIN(status->set, 0, UNSIGNED_SHORT_MAX);
	message.state.round = (word)PIN(status->round, 0, UNSIGNED_SHORT_MAX);
	message.state.lives = (word)PIN(status->lives, 0, UNSIGNED_SHORT_MAX);
	message.state.living = (word)PIN(status->living, 0, UNSIGNED_SHORT_MAX);
	message.state.team_score = status->team_score;
	memcpy(message.scores, status->scores, count * sizeof(message.scores[0]));
	/* (the header's game time aside, as distributed_send fills it) */
	if (network_firefight.sent_time != NONE && size == network_firefight.sent_size &&
		!memcmp(&message.state, &network_firefight.sent.state, size - sizeof(message.header)) &&
		game_time_get() - network_firefight.sent_time < NETWORK_FIREFIGHT_SEND_TICKS)
	{
		return;
	}
	network_firefight.sent = message;
	network_firefight.sent_size = size;
	network_firefight.sent_time = game_time_get();
	distributed_send(&message, _distributed_message_firefight_state, (short)count, size, _distributed_to_clients);
}

boolean network_firefight_status(struct network_firefight_status *status)
{
	if (network_firefight.received_time == NONE)
		return FALSE;
	*status = network_firefight.received;
	return TRUE;
}

boolean network_firefight_handles_message(word const *message, word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	memcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_firefight_state;
}

/* (the host's alone, whole) */
void network_firefight_handle_message(long machine_index, word const *data, word size)
{
	struct distributed_firefight_state_message message;
	struct network_firefight_status *status = &network_firefight.received;
	short index;

	if (machine_index != NONE || game_connection() != _game_connection_network_client ||
		size < firefight_state_message_size(0) || size > sizeof(message))
	{
		return;
	}
	memcpy(&message, data, size);
	if (message.header.type != _distributed_message_firefight_state ||
		message.header.count > NETWORK_FIREFIGHT_MAXIMUM_SCORES ||
		size != firefight_state_message_size(message.header.count) ||
		(message.state.flags & ~((1 << NUMBER_OF_NETWORK_FIREFIGHT_FLAGS) - 1)) != 0 ||
		message.state.waves_per_round == 0 || message.state.wave >= message.state.waves_per_round ||
		message.state.reserved[0] || message.state.reserved[1] || message.state.reserved[2])
	{
		return;
	}
	for (index = 0; index < message.header.count; index++)
	{
		if (message.scores[index].player_index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS ||
			message.scores[index].reserved != 0)
		{
			return;
		}
	}
	if (network_firefight.received_time != NONE && message.header.game_time < network_firefight.received_time)
		return;

	/* (logged when its set, round, wave, lives or end change) */
	if (network_firefight.received_time == NONE || status->set != (short)MIN(message.state.set, SHORT_MAX) ||
		status->round != (short)MIN(message.state.round, SHORT_MAX) || status->wave != message.state.wave ||
		status->lives != (short)MIN(message.state.lives, SHORT_MAX) ||
		status->game_over != TEST_FLAG(message.state.flags, _network_firefight_game_over_bit))
	{
		platform_log("firefight: the host's state: set %d round %d wave %d, %d lives, team score %ld%s",
			(int)message.state.set + 1, (int)message.state.round + 1, (int)message.state.wave + 1,
			(int)message.state.lives, (long)message.state.team_score,
			TEST_FLAG(message.state.flags, _network_firefight_game_over_bit) ? ", game over" : "");
	}
	network_firefight.received_time = message.header.game_time;
	memset(status, 0, sizeof(*status));
	status->active = TEST_FLAG(message.state.flags, _network_firefight_active_bit);
	status->game_over = TEST_FLAG(message.state.flags, _network_firefight_game_over_bit);
	status->state = message.state.state;
	status->stage = message.state.stage;
	status->wave = message.state.wave;
	status->waves_per_round = message.state.waves_per_round;
	status->set = (short)MIN(message.state.set, SHORT_MAX);
	status->round = (short)MIN(message.state.round, SHORT_MAX);
	status->lives = (short)MIN(message.state.lives, SHORT_MAX);
	status->living = (short)MIN(message.state.living, SHORT_MAX);
	status->team_score = message.state.team_score;
	status->score_count = message.header.count;
	memcpy(status->scores, message.scores, message.header.count * sizeof(message.scores[0]));
}
