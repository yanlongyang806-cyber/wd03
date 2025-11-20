#pragma once

int TestServer_PushMetric(const char *pcScope, const char *pcName, float val);
void TestServer_ClearMetric(const char *pcScope, const char *pcName);

int TestServer_GetMetricCount(const char *pcScope, const char *pcName);
float TestServer_GetMetricTotal(const char *pcScope, const char *pcName);
float TestServer_GetMetricAverage(const char *pcScope, const char *pcName);
float TestServer_GetMetricMinimum(const char *pcScope, const char *pcName);
float TestServer_GetMetricMaximum(const char *pcScope, const char *pcName);