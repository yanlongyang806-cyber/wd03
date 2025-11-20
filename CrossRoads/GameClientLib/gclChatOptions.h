#ifndef CHAT_OPTIONS_H_
#define CHAT_OPTIONS_H_

void gclChatOptionsEnable(void);
void gclChatOptionsDisable(void);

LATELINK;
void gameSpecific_gclChatOptions_Init(const char* pchCategory);

#endif //BASIC_OPTIONS_H_