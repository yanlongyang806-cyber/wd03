#pragma once

void TestServer_IssueReport(const char *pcScope, const char *pcName);
void TestServer_QueueReport(const char *pcScope, const char *pcName);

void TestServer_InitReports(void);
void TestServer_CheckReports(void);
void TestServer_CancelAllReports(void);