#ifndef PRODUCTKEYCMDS_WEBINTERFACE_H
#define PRODUCTKEYCMDS_WEBINTERFACE_H

bool initWebInterface(void);

typedef struct NetComm NetComm;
NetComm * getWebComm(void);

void updateWebInterface(void);

#endif
