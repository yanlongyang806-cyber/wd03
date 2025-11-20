// Init/tick functions
void gclScript_Init(void);
void gclScript_Tick(void);

// Functions to queue up event notifications
void gclScript_QueueChat(const char *pcChannel, const char *pcSender, const char *pcMessage);
void gclScript_QueueNotify(const char *pcName, const char *pcObject, const char *pcString);

void gclScript_StateEnterLeave(bool bEnter, const char *pchState);