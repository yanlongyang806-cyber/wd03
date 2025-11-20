#pragma once

#define WI_ACCOUNTS_DIR "/accounts/"

typedef struct ASWebRequest ASWebRequest;

bool wiHandleAccounts(SA_PARAM_NN_VALID ASWebRequest *pWebRequest);