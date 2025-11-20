#pragma once

typedef void (*FetchAutoBillsSinceCallback)(bool bSuccess, UserData pUserData);

void btFetchAutoBillsSince(U32 uSecondsSince2000, SA_PARAM_OP_VALID FetchAutoBillsSinceCallback pCallback, SA_PARAM_OP_VALID void *pUserData);