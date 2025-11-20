#pragma once

#define WI_SUBSCRIPTIONS_DIR "/subscriptions/"

typedef struct ASWebRequest ASWebRequest;

bool wiHandleSubscriptions(SA_PARAM_NN_VALID ASWebRequest *pWebRequest);