#pragma once
GCC_SYSTEM

typedef struct UIComboBox UIComboBox;
/////////////////////////////////////////////////////////
// For directly attaching data from the Client to Tickets

// Set *ppStruct to the data and *ppti to the parse table
// And remember that users can submit tickets when not actually logged in to a character
typedef void (*CategoryCustomDataFunc) (void **ppStruct, ParseTable** ppti, char **estrLabel);

bool cBugAddCustomDataCallback (const char *category, CategoryCustomDataFunc callback);
bool cBugRemoveCustomDataCallback (const char *category, CategoryCustomDataFunc callback);

/////////////////////////////////////////////////////////
// For Attaching Data on the Server based on Client Input

// Func to get a text description of a model element from the Combo Box;
// Used for the label descriptor function (the text that is set as the label descriptor and is sent off to the Game Server for further processing)
typedef void (*CategoryComboBoxTextFunc) (char **, const void *);

// Function to get the selected model element from the special UI Gen
typedef const void * (*CategoryComboBoxSelectedFunc) (void);

// Add a combo box for user when _category_ is selected
void cBug_AddCategoryChoiceCallbacks(const char *category, CategoryComboBoxTextFunc textf, 
									 CategoryComboBoxTextFunc labelf, CategoryComboBoxSelectedFunc selectedf, 
									 const char *pGenName);

void Category_SendTicketUpdateRequest(void);

