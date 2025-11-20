#pragma once

#define WI_ADMIN_DIR "/admin/"

typedef struct ASWebRequest ASWebRequest;

bool wiHandleAdmin(SA_PARAM_NN_VALID ASWebRequest *pWebRequest);