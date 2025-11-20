#pragma once

typedef struct ChatUser ChatUser;
typedef struct NetLink NetLink;

void userSendActivityStatus(SA_PARAM_NN_VALID ChatUser *user, NetLink *originator);