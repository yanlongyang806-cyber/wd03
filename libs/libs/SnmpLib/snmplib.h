#pragma once

// Client functionality

bool snmpGetString(const char *location, int port, const char *communitystr, const char *oidstr, char **estr);
bool snmpGetInt(const char *location, int port, const char *communitystr, const char *oidstr, int *output);

// Server ("agent") functionality

bool snmpServerInit(int port, const char *system_description);
void snmpServerShutdown(void);

void snmpServerOncePerFrame(void);

typedef struct SnmpValueHandle SnmpValueHandle;

SnmpValueHandle * snmpServerCreateString(const char *oidstr, const char *initial_value);
SnmpValueHandle * snmpServerCreateInt(const char *oidstr, int initial_value);
void snmpServerDeleteHandle(SnmpValueHandle *handle);

void snmpServerSetValueString(SnmpValueHandle *handle, const char *value);
void snmpServerSetValueInt(SnmpValueHandle *handle, int value);

#define CRYPTIC_SNMP_PREFIX "1.3.6.1.4.1.9.29330"
#define DEFAULT_SNMP_PORT 161
