#ifndef NETIPFILTER_H
#define NETIPFILTER_H

bool ipfGroupExists(const char *groupName);

bool ipfIsLocalIp(U32 uIP);
bool ipfIsTrustedIp(U32 uIP);
bool ipfIsIpInGroup(const char *groupName, U32 uIP);

bool ipfIsLocalIpString(const char *ipstr);
bool ipfIsTrustedIpString(const char *ipstr);
bool ipfIsIpStringInGroup(const char *groupName, const char *ipstr);

bool ipfAddFilter(const char *groupName, const char *filterString);

void ipfResetFilters(bool full);
void ipfLoadDefaultFilters(void);

#endif
