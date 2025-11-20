#pragma once

void TestServer_InitSchedules(void);
void TestServer_ScheduleTick(void);

void TestServer_AddScheduleEntry(const char *script, int day, int hr, int min, int sec, int interval, int repeats, bool important);
void TestServer_ClearSchedules(void);