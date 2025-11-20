#ifndef LOGINSERVER_TOKENPARSING_H
#define LOGINSERVER_TOKENPARSING_H

typedef struct LoginLink LoginLink;
typedef struct AccountPermissionStruct AccountPermissionStruct;

bool aslProcessSpecialTokens(LoginLink *loginLink, CONST_STRING_EARRAY eaTokens, AccountPermissionStruct *pProductPermissions);
bool aslLogin2_DoPermissionTokensAllowPlay(CONST_STRING_EARRAY tokens);
#endif