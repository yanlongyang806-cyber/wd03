/*
 * Simple CPU usage profiling module (GameServerLib)
 *
 * The goal here is to take basic CPU cycle counts for select GameServer threads and place them in a graph on a GameClient.
 * That way content creators can have an idea of what in their maps is taking up server CPU cycles.
 *
 * It should help answer these questions: "If the number of civilians in a large static zone is reduced from 500 to 200, what
 * is the impact on GameServer performance?"
 *
 * To enable, run this command as an AL9 player while logged into a GameServer: simpleCpuUsage 1
 *
 * Notes
 * 1) Only one player at a time can view the graph. The graph data is placed on that Entity->Player->SimpleCpuUsageData.
 *    The side effect is that if you enable it for yourself while someone else is viewing the graph, it will disappear for the other player.
 * 2) When simpleCpuUsage is 0, then no CPU usage data is collected at all and Entity->Player->SimpleCpuUsageData will be NULL (destroyed)
 */
#pragma once

void gslSimpleCpu_CaptureFrames(void);
