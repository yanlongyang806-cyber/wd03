#pragma once

typedef bool (*StayUpFunc)(void *pUserData);

bool StayUp(int argc, const char *const *argv,
			StayUpFunc pSafeToStartFunc, void *pSafeToStartUserData,
			StayUpFunc pTickFunc, void *pTickUserData);

bool StartedByStayUp(void);
int StayUpCount(void); //returns 1 the first time, 2 the second time, etc.
void CancelStayUp(void); //kills the parent so that it can't stayup us any more
