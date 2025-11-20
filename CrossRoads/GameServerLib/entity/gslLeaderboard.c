#include "Leaderboard.h"

AUTO_STARTUP(LeaderboardServer) ASTRT_DEPS(PlayerStats);
void leaderboardServer_load(void)
{
	leaderboard_load();
}