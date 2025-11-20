#pragma once

typedef struct XmppClientState XmppClientState;

void XMPP_ValidateLogin(XmppClientState *state, char *login, char *password);
void XMPP_DestroyValidator(XmppClientState *state);

bool XMPP_IsLoggedIn(const XmppClientState *state);
bool CheckValidatorStatus(XmppClientState *state);
void XMPP_LoginValidatorTick(void);