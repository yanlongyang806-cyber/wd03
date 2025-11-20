// This file contains generic interfaces to system-specific security interfaces.

#ifndef CRYPTIC_OSPERMISSIONS_H
#define CRYPTIC_OSPERMISSIONS_H

// Enable operating system privilege to bypass regular file ACLs.
bool EnableBypassReadAcls(bool enable);

#endif  // CRYPTIC_OSPERMISSIONS_H
