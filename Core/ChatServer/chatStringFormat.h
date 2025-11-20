#pragma once

typedef struct ChatUser ChatUser;
typedef struct ChatTranslation ChatTranslation;

void ChatServer_Translate(SA_PARAM_NN_VALID ChatUser *user, ChatTranslation *translation, char **msg);