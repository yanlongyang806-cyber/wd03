#include "XMPP_Gateway.h"
#include "XMPP_Login.h"
#include "XMPP_Net.h"

#include "accountnet.h"
#include "accountCommon.h"
#include "error.h"
#include "net.h"
#include "logging.h"
#include "timing_profiler.h"
#include "UtilitiesLibEnums.h"
#include "accountnet_h_ast.h"

static XmppClientState **seaValidatorQueue = NULL;

// Start checking the username and password for login.
void XMPP_ValidateLogin(XmppClientState *state, char *login, char *password)
{
	char id[21];
	char ip[17];
	char passlen[17];
	sprintf(passlen, "%d", strlen(password));
	servLogWithPairs(LOG_XMPP_GENERAL, "XmppBeginAuth",
		"id", XMPP_UniqueClientIdStr(state, id, sizeof(id)),
		"ip", XMPP_GetIpStr(state, ip, sizeof(ip)),
		"account", login,
		"passlen", passlen, NULL);
	state->validator = accountValidatorCreate();
	accountValidatorRequestTicket(state->validator, login, password);
	eaPush(&seaValidatorQueue, state);
}

// Stop validation and destroy the validator associated with a client.
void XMPP_DestroyValidator(XmppClientState *state)
{
	// Remove any pending validator request.
	if (state->validator && !state->ticket)
	{
		int removed = eaFindAndRemoveFast(&seaValidatorQueue, state);
		devassert(removed != -1);
	}

	// Destroy the validator.
	if (state->validator)
	{
		accountValidatorDestroy(state->validator);
		state->validator = NULL;
	}

	// Destroy the login ticket.
	if (state->ticket)
	{
		StructDestroy(parse_AccountTicket, state->ticket);
		state->ticket = NULL;
	}
}

// Return true if this client is totally logged in.
bool XMPP_IsLoggedIn(const XmppClientState *state)
{
	return state && state->ticket && state->resource && !xmpp_ClientIsClosing(state->link);
}

// Return true if this client is allowed to login with XMPP.
static bool AllowedToLoginWithXmpp(XmppClientState *state)
{
	bool hasPermission = false;
	bool hasGmAccess = false;
	bool banned = false;

	// Trusted links are always allowed to log in.
	if (XMPP_Trusted(state))
	{
		state->iAccessLevel = ACCESS_DEBUG;
		return true;
	}

	// Allow the user to log in if they have XMPP permission for any product for which they are not banned.
	EARRAY_CONST_FOREACH_BEGIN(state->ticket->ppPermissions, i, n);
	{
		AccountPermissionStruct *permissions = state->ticket->ppPermissions[i];
		if (permissions->iAccessLevel >= 4)
		{
			state->iAccessLevel = max(state->iAccessLevel , permissions->iAccessLevel);
			hasGmAccess = true;
		}
		else if (!hasGmAccess && permissionsGame(permissions, ACCOUNT_PERMISSION_XMPP) && !(permissions->uFlags & ACCOUNTPERMISSION_BANNED))
		{
			state->iAccessLevel  = max(state->iAccessLevel , permissions->iAccessLevel);
			hasPermission = true;
		}
	}
	EARRAY_FOREACH_END;
	return hasPermission || hasGmAccess;
}

// Check to see if validation has completed for a client waiting to log in.
// Return true if this client has completed validation and should be removed from the list.
bool CheckValidatorStatus(XmppClientState *state)
{
	AccountValidatorResult result;
	int removed = -1;
	char id[21];
	char ip[17];
	const char *reason;

	// Process validator.
	accountValidatorTick(state->validator);

	// Handle result.
	result = accountValidatorGetResult(state->validator);
	switch(result)
	{
		// Still waiting for a response from the Account Server
	case ACCOUNTVALIDATORRESULT_STILL_PROCESSING:
		break;

		// Error servicing validation request
	case ACCOUNTVALIDATORRESULT_FAILED_CONN_TIMEOUT:
	case ACCOUNTVALIDATORRESULT_FAILED_GENERIC:
		removed = eaFindAndRemoveFast(&seaValidatorQueue, state);
		devassert(removed != -1);
		servLogWithPairs(LOG_XMPP_GENERAL, "XmppAuthFinished",
			"id", XMPP_UniqueClientIdStr(state, id, sizeof(id)),
			"ip", XMPP_GetIpStr(state, ip, sizeof(ip)),
			"success", "0",
			"temporary", "1",
			"type", result == ACCOUNTVALIDATORRESULT_FAILED_CONN_TIMEOUT ? "ConnTimeout" : "Generic", NULL);
		XMPP_AuthComplete(state, false, XMPP_SaslError_TemporaryAuthFailure,
			result == ACCOUNTVALIDATORRESULT_FAILED_CONN_TIMEOUT ? "Internal connection timeout" : "Unknown internal error");
		accountValidatorDestroy(state->validator);
		state->validator = NULL;
		break;

		// Response: authentication not successful
	case ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_FAILED:
		removed = eaFindAndRemoveFast(&seaValidatorQueue, state);
		devassert(removed != -1);
		reason = accountValidatorGetFailureReason(state->validator);
		servLogWithPairs(LOG_XMPP_GENERAL, "XmppAuthFinished",
			"id", XMPP_UniqueClientIdStr(state, id, sizeof(id)),
			"ip", XMPP_GetIpStr(state, ip, sizeof(ip)),
			"success", "0",
			"temporary", "0",
			"type", "AuthFailed",
			"reason", reason ? reason : "", NULL);
		XMPP_AuthComplete(state, false, XMPP_SaslError_NotAuthorized, reason);
		accountValidatorDestroy(state->validator);
		state->validator = NULL;
		break;

		// Response: account authenticated
	case ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_SUCCESS:
		{
			AccountTicketSigned signedTicket = {0};
			char *pTicketString = NULL;

			// Get login ticket string.
			removed = eaFindAndRemoveFast(&seaValidatorQueue, state);
			devassert(removed != -1);
			accountValidatorGetTicket(state->validator, &pTicketString);
			ParserReadText(pTicketString, parse_AccountTicketSigned, &signedTicket, 0);
			estrDestroy(&pTicketString);
			if (!signedTicket.ticketText || !signedTicket.strTicketTPI)
			{
				const char error[] = "Login ticket lookup failed";
				servLogWithPairs(LOG_XMPP_GENERAL, "XmppAuthFinished",
					"id", XMPP_UniqueClientIdStr(state, id, sizeof(id)),
					"ip", XMPP_GetIpStr(state, ip, sizeof(ip)),
					"success", "0",
					"temporary", "1",
					"type", "MissingTicket", NULL);
				Errorf("%s: \"%s\"", error, pTicketString);
				XMPP_AuthComplete(state, false, XMPP_SaslError_TemporaryAuthFailure, error);
				accountValidatorDestroy(state->validator);
				state->validator = NULL;
				return true;
			}

			// Create login ticket.
			state->ticket = StructCreate(parse_AccountTicket);
			ParserReadTextSafe(signedTicket.ticketText, signedTicket.strTicketTPI, signedTicket.uTicketCRC, 
				parse_AccountTicket, state->ticket, 0);
			StructDeInit(parse_AccountTicketSigned, &signedTicket);
			if (!state->ticket->accountID || !state->ticket->accountName[0] || !state->ticket->displayName[0])
			{
				const char error[] = "Login ticket creation failed";
				servLogWithPairs(LOG_XMPP_GENERAL, "XmppAuthFinished",
					"id", XMPP_UniqueClientIdStr(state, id, sizeof(id)),
					"ip", XMPP_GetIpStr(state, ip, sizeof(ip)),
					"success", "0",
					"temporary", "1",
					"type", "BrokenTicket", NULL);
				Errorf("%s", error);
				XMPP_AuthComplete(state, false, XMPP_SaslError_TemporaryAuthFailure, error);
				accountValidatorDestroy(state->validator);
				state->validator = NULL;
				return true;
			}

			// Verify that this account is authorized to log in.
			// This checks: Trusted with XMPP Server and Ticket permissions
			if (!AllowedToLoginWithXmpp(state))
			{
				servLogWithPairs(LOG_XMPP_GENERAL, "XmppAuthFinished",
					"id", XMPP_UniqueClientIdStr(state, id, sizeof(id)),
					"ip", XMPP_GetIpStr(state, ip, sizeof(ip)),
					"success", "0",
					"temporary", "0",
					"type", "NotAllowed", NULL);
				XMPP_AuthComplete(state, false, XMPP_SaslError_NotAuthorized, "This account is not allowed to login with XMPP.");
				accountValidatorDestroy(state->validator);
				state->validator = NULL;
				return true;
			}

			// Don't let people with invalid display names log in.
			if (state->ticket->bInvalidDisplayName)
			{
				servLogWithPairs(LOG_XMPP_GENERAL, "XmppAuthFinished",
					"id", XMPP_UniqueClientIdStr(state, id, sizeof(id)),
					"ip", XMPP_GetIpStr(state, ip, sizeof(ip)),
					"success", "0",
					"temporary", "0",
					"type", "InvalidDisplayName", NULL);
				XMPP_AuthComplete(state, false, XMPP_SaslError_AccountDisabled, "Invalid display name");
				accountValidatorDestroy(state->validator);
				state->validator = NULL;
				return true;
			}

			// Inform client of authentication success.
			servLogWithPairs(LOG_XMPP_GENERAL, "XmppAuthFinished",
				"id", XMPP_UniqueClientIdStr(state, id, sizeof(id)),
				"ip", XMPP_GetIpStr(state, ip, sizeof(ip)),
				"success", "1", NULL);
			XMPP_AuthComplete(state, true, 0, NULL);
			accountValidatorDestroy(state->validator);
			state->validator = NULL;
		} break;

	default:
		devassert(0);
		removed = eaFindAndRemoveFast(&seaValidatorQueue, state);
	}

	// Return true if we removed the client from the validator queue.
	return removed != -1;
}

void XMPP_LoginValidatorTick(void)
{
	int i, size;

	PERFINFO_AUTO_START("XMPP Login Validator", 1);
	commMonitor(accountCommDefault());
	size = eaSize(&seaValidatorQueue) ;
	for (i = 0; i < size; i++)
	{
		bool removed = CheckValidatorStatus(seaValidatorQueue[i]);
		if (removed)
		{
			i--;
			size--;
		}
	}
	PERFINFO_AUTO_STOP();
}