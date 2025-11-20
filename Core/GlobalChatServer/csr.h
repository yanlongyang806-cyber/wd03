#ifndef _CSR_H
#define _CSR_H

void userCsrSilence(ChatUser *user,char *target_name,char *minutes);
void userCsrRenameable(ChatUser *user, char *target_name);
void csrSilenceAll(ChatUser *user);
void csrUnsilenceAll(ChatUser *user);
void csrSendAll(ChatUser *user,char *msg);
void csrSendAllAnon(char *msg); 
void csrSysMsgSendAll(ChatUser *user,char *msg);
void csrRenameAll(ChatUser *user);
void csrStatus(ChatUser * user, char *target_name);
#endif
