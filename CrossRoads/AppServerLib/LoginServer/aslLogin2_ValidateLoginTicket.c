/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_ValidateLoginTicket.h"
#include "aslLogin2_Error.h"
#include "net.h"
#include "accountnet.h"
#include "stdtypes.h"
#include "textparser.h"
#include "timing_profiler.h"

#include "AutoGen/aslLogin2_ValidateLoginTicket_c_ast.h"
#include "AutoGen/accountnet_h_ast.h"

AUTO_ENUM;
typedef enum LoginValidatorPhase
{
    ValidatorPhase_ValidateTicket,
    ValidatorPhase_GenerateOneTimeCode,
    ValidatorPhase_ValidateOneTimeCode,
    ValidatorPhase_SetMachine,
} LoginValidatorPhase;

AUTO_STRUCT;
typedef struct ValidateLoginTicketState
{
    ContainerID accountID;
    U32 ticketID;

    AccountValidator *validator;                    NO_AST
    LoginValidatorPhase phase;

    bool failed;
    char *errorString;                              AST(ESTRING)
    const char *errorMessageKey;                    NO_AST

    // Completion callback data.
    ValidateLoginTicketCB cbFunc;                   NO_AST
    GenerateOneTimeCodeCB generateOTCCBFunc;        NO_AST
    ValidateOneTimeCodeCB validateOTCCBFunc;        NO_AST
    SetMachineNameCB setMachineNameCBFunc;          NO_AST

    void *userData;                                 NO_AST
} ValidateLoginTicketState;

static EARRAY_OF(ValidateLoginTicketState) s_ActiveValidateTicketStates = NULL;

void
LogResult(const char *funcName, ValidateLoginTicketState *validateState)
{
    // Log success or failure.
    if ( validateState->failed )
    {
        aslLogin2_Log("%s: validation failed for account %u.  %s", funcName, validateState->accountID, validateState->errorString);
    }
    else
    {
        aslLogin2_Log("%s: validation succeeded for account %u.", funcName, validateState->accountID);
    }
}

static void
CleanupState(ValidateLoginTicketState *validateState)
{
    // Clean up the validator.
    if ( validateState->validator )
    {
        accountValidatorDestroy(validateState->validator);
        validateState->validator = NULL;
    }

    // Clean up the state.
    StructDestroy(parse_ValidateLoginTicketState, validateState);
}

static void
ValidateLoginTicketComplete(ValidateLoginTicketState *validateState, AccountTicket *accountTicket)
{
    // Log success or failure.
    LogResult("aslLogin2_ValidateLoginTicket", validateState);

    // Notify the caller of the results.  Note that callback function owns the ticket and must ensure that it is eventually freed.
    if ( validateState->cbFunc )
    {
        (validateState->cbFunc)(validateState->accountID, accountTicket, !validateState->failed, validateState->errorMessageKey, validateState->userData);
    }

    CleanupState(validateState);
}

static void
GenerateOneTimeCodeComplete(ValidateLoginTicketState *validateState)
{
    // Log success or failure.
    LogResult("aslLogin2_GenerateOneTimeCode", validateState);

    // Notify the caller of the results.  Note that callback function owns the ticket and must ensure that it is eventually freed.
    if ( validateState->generateOTCCBFunc )
    {
        (validateState->generateOTCCBFunc)(validateState->accountID, !validateState->failed, validateState->errorMessageKey, validateState->userData);
    }

    CleanupState(validateState);
}

static void
ValidateOneTimeCodeComplete(ValidateLoginTicketState *validateState)
{
    // Log success or failure.
    LogResult("aslLogin2_ValidateOneTimeCode", validateState);

    // Notify the caller of the results.  Note that callback function owns the ticket and must ensure that it is eventually freed.
    if ( validateState->validateOTCCBFunc )
    {
        (validateState->validateOTCCBFunc)(validateState->accountID, !validateState->failed, validateState->errorMessageKey, validateState->userData);
    }

    CleanupState(validateState);
}

void
SetMachineNameComplete(ValidateLoginTicketState *validateState)
{
    // Log success or failure.
    LogResult("aslLogin2_SetMachineName", validateState);

    // Notify the caller of the results.  Note that callback function owns the ticket and must ensure that it is eventually freed.
    if ( validateState->setMachineNameCBFunc )
    {
        (validateState->setMachineNameCBFunc)(validateState->accountID, !validateState->failed, validateState->errorMessageKey, validateState->userData);
    }

    CleanupState(validateState);
}

void
aslLogin2_ValidateLoginTicket(ContainerID accountID, U32 ticketID, ValidateLoginTicketCB cbFunc, void *userData)
{
    ValidateLoginTicketState *validateState;

    validateState = StructCreate(parse_ValidateLoginTicketState);
    validateState->accountID = accountID;
    validateState->ticketID = ticketID;
    validateState->cbFunc = cbFunc;
    validateState->userData = userData;
    validateState->phase = ValidatorPhase_ValidateTicket;

    validateState->validator = accountValidatorAddValidateRequest(accountValidatorGetPersistent(), accountID, ticketID);

    if ( validateState->validator )
    {
        eaPush(&s_ActiveValidateTicketStates, validateState);
    }
    else
    {
        ValidateLoginTicketComplete(validateState, NULL);
    }
}

void
aslLogin2_GenerateOneTimeCode(ContainerID accountID, const char *machineID, U32 clientIPAddress, GenerateOneTimeCodeCB cbFunc, void *userData)
{
    ValidateLoginTicketState *validateState;

    validateState = StructCreate(parse_ValidateLoginTicketState);
    validateState->accountID = accountID;
    validateState->generateOTCCBFunc = cbFunc;
    validateState->userData = userData;
    validateState->phase = ValidatorPhase_GenerateOneTimeCode;

    validateState->validator = accountValidatorGenerateOneTimeCode(accountValidatorGetPersistent(), accountID, machineID, clientIPAddress);

    if ( validateState->validator )
    {
        eaPush(&s_ActiveValidateTicketStates, validateState);
    }
    else
    {
        validateState->failed = true;
        validateState->errorMessageKey = "Login2_InternalError";
        estrConcatf(&validateState->errorString, "Failed to create validator.");
        GenerateOneTimeCodeComplete(validateState);
    }
}

void
aslLogin2_ValidateOneTimeCode(ContainerID accountID, const char *machineID, const char *oneTimeCode, const char *machineName, U32 clientIPAddress, ValidateOneTimeCodeCB cbFunc, void *userData)
{
    ValidateLoginTicketState *validateState;

    validateState = StructCreate(parse_ValidateLoginTicketState);
    validateState->accountID = accountID;
    validateState->validateOTCCBFunc = cbFunc;
    validateState->userData = userData;
    validateState->phase = ValidatorPhase_ValidateOneTimeCode;

    validateState->validator = accountValidatorAddOneTimeCodeValidation(accountValidatorGetPersistent(), accountID, machineID, oneTimeCode, machineName, clientIPAddress);

    if ( validateState->validator )
    {
        eaPush(&s_ActiveValidateTicketStates, validateState);
    }
    else
    {
        validateState->failed = true;
        validateState->errorMessageKey = "Login2_InternalError";
        estrConcatf(&validateState->errorString, "Failed to create validator.");

        ValidateOneTimeCodeComplete(validateState);
    }
}

void
aslLogin2_SetMachineName(ContainerID accountID, const char *machineID, const char *machineName, U32 clientIPAddress, SetMachineNameCB cbFunc, void *userData)
{
    ValidateLoginTicketState *validateState;

    validateState = StructCreate(parse_ValidateLoginTicketState);
    validateState->accountID = accountID;
    validateState->setMachineNameCBFunc = cbFunc;
    validateState->userData = userData;
    validateState->phase = ValidatorPhase_SetMachine;

    validateState->validator = accountValidatorSaveNextMachine(accountValidatorGetPersistent(), accountID, machineID, machineName, clientIPAddress);

    if ( validateState->validator )
    {
        eaPush(&s_ActiveValidateTicketStates, validateState);
    }
    else
    {
        validateState->failed = true;
        validateState->errorMessageKey = "Login2_InternalError";
        estrConcatf(&validateState->errorString, "Failed to create validator.");

        SetMachineNameComplete(validateState);
    }
}

static AccountTicket *
GetAccountTicket(AccountValidator *validator)
{
    static char *accountTicketString = NULL;
    AccountTicketSigned *signedTicket = StructCreate(parse_AccountTicketSigned);
    AccountTicket *ticket = NULL;

    // Make sure the string is empty.
    estrClear(&accountTicketString);

    // Get the signed ticket from the validator in string form.
    accountValidatorGetTicket(validator, &accountTicketString);

    // Read the signed ticket string into a struct.
    ParserReadText(accountTicketString, parse_AccountTicketSigned, signedTicket, 0);

    if ( signedTicket->ticketText )
    {
        // Create and read the ticket struct the string form.
        ticket = StructCreate(parse_AccountTicket);
        ParserReadTextSafe(signedTicket->ticketText, signedTicket->strTicketTPI, signedTicket->uTicketCRC, parse_AccountTicket, ticket, 0);
    }

    // Clean up the signed ticket.
    StructDestroy(parse_AccountTicketSigned, signedTicket);

    return ticket;
}

static const char *
ValidatorResultToError(AccountValidatorResult validatorResult, char **logErrorString)
{
    const char *errorMessageKey = "";

    switch (validatorResult)
    {
    case ACCOUNTVALIDATORRESULT_FAILED_CONN_TIMEOUT:
        errorMessageKey = "Login2_AccountTicketTimeout";
        estrPrintf(logErrorString, "Account ticket validation timeout");
        break;
    case ACCOUNTVALIDATORRESULT_FAILED_GENERIC:
        errorMessageKey = "Login2_AccountTicketFailure";
        estrPrintf(logErrorString, "Account ticket validation failure");
        break;
    case ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_FAILED:
        errorMessageKey = "Login2_AccountTicketAuthFailed";
        estrPrintf(logErrorString, "Account ticket validation auth failed");
        break;
    case ACCOUNTVALIDATORRESULT_OTC_FAILED:
        errorMessageKey = "Login2_AccountTicketOTCFailure";
        estrPrintf(logErrorString, "Account ticket validation One Time Code failed");
        break;
    // Success cases will only get here if they are received during the incorrect phase.
    case ACCOUNTVALIDATORRESULT_OTC_GENERATED:
    case ACCOUNTVALIDATORRESULT_OTC_SUCCESS:
    case ACCOUNTVALIDATORRESULT_SAVENEXTMACHINE_SUCCESS:
    default:
        errorMessageKey = "Login2_InternalError";
        estrPrintf(logErrorString, "Account ticket validation received invalid response. %d", validatorResult);
        break;
    }

    return errorMessageKey;
}

void
aslLogin2_ValidateLoginTicketTick(void)
{
    int i;

    PERFINFO_AUTO_START_FUNC();
    for ( i = eaSize(&s_ActiveValidateTicketStates) - 1; i >= 0; i-- )
    {
        ValidateLoginTicketState *validateState = s_ActiveValidateTicketStates[i];

        // Tick validator.
        accountValidatorTick(validateState->validator);

        // Check if the validator has results to report.
        if ( accountValidatorIsReady(validateState->validator) )
        {
            AccountValidatorResult validatorResult;
            AccountTicket *accountTicket = NULL;

            PERFINFO_AUTO_START("Login2 Validate Ticket Ready", 1);

            // We can remove it from the active list since results are ready now.
            eaRemoveFast(&s_ActiveValidateTicketStates, i);

            // Remove the validator from the list that the accountValidator module is processing.
            accountValidatorRemoveValidateRequest(accountValidatorGetPersistent(), validateState->validator);

            // Get the validator result.
            validatorResult = accountValidatorGetResult(validateState->validator);

            if ( validateState->phase == ValidatorPhase_ValidateTicket )
            {
                // Handle ticket validation response.
                if ( validatorResult == ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_SUCCESS )
                {
                    // Ticket ID is good.
                    accountTicket = GetAccountTicket(validateState->validator);
                    if ( accountTicket == NULL )
                    {
                        validateState->failed = true;
                        validateState->errorMessageKey = "Login2_NoAccountTicket";
                        estrConcatf(&validateState->errorString, "Account ticket ID validation succeeded, but no account ticket was returned.");
                    }
                }
                else
                {
                    validateState->failed = true;
                    validateState->errorMessageKey = ValidatorResultToError(validatorResult, &validateState->errorString);
                }

                // We are done with this ticket.
                ValidateLoginTicketComplete(validateState, accountTicket);

            }
            else if ( validateState->phase == ValidatorPhase_GenerateOneTimeCode )
            {
                // Handle generate one time code.
                if ( validatorResult != ACCOUNTVALIDATORRESULT_OTC_GENERATED )
                {
                    validateState->failed = true;
                    validateState->errorMessageKey = ValidatorResultToError(validatorResult, &validateState->errorString);
                }

                GenerateOneTimeCodeComplete(validateState);
            }
            else if ( validateState->phase == ValidatorPhase_ValidateOneTimeCode )
            {
                // Handle validate one time code.
                if ( validatorResult != ACCOUNTVALIDATORRESULT_OTC_SUCCESS )
                {
                    validateState->failed = true;
                    validateState->errorMessageKey = ValidatorResultToError(validatorResult, &validateState->errorString);
                }

                ValidateOneTimeCodeComplete(validateState);
            }
            else if ( validateState->phase == ValidatorPhase_SetMachine )
            {
                // Handle set machine name.
                if ( validatorResult != ACCOUNTVALIDATORRESULT_SAVENEXTMACHINE_SUCCESS )
                {
                    validateState->failed = true;
                    validateState->errorMessageKey = ValidatorResultToError(validatorResult, &validateState->errorString);
                }

                SetMachineNameComplete(validateState);
            }

            PERFINFO_AUTO_STOP();
        }
    }
    PERFINFO_AUTO_STOP_FUNC();
}

#include "AutoGen/aslLogin2_ValidateLoginTicket_c_ast.c"