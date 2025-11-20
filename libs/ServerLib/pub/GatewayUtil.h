/***************************************************************************
 
 
 ***************************************************************************/
#ifndef GATEWAYUTIL_H__
#define GATEWAYUTIL_H__
#pragma once

extern bool gateway_FindDeployDir(char **pestr);
	// Finds the root of the Gateway deploy directory.

bool gateway_FindScriptDir(char **pestr);
	// Finds the script directory

extern bool gateway_IsLocked(void);
	// If true, Gateway is locked. No new logins are allowed.

LATELINK;
void GatewayNotifyLockStatus(bool bLocked);
	// Should be provided by any Gateway server.


#endif /* #ifndef GATEWAYUTIL_H__ */

/* End of File */
