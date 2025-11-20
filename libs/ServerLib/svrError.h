/***************************************************************************



***************************************************************************/

#ifndef SERVERERROR_H
#define SERVERERROR_H

typedef struct ErrorMessage ErrorMessage;

// Generic server error handler
void serverErrorfCallback(ErrorMessage* errMsg, void *userdata);

#endif
