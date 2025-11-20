#pragma once
GCC_SYSTEM

// take a look at an example to see how to use this struct
typedef struct ConsoleHistory ConsoleHistory;
typedef struct ConsoleAutoComplete ConsoleAutoComplete;

typedef struct ConsoleDebugMenu {
	char	cmd;						// if empty, print only helptext
	char*	helptext;					// if both empty, end of list
	void	(*singlekeyfunc)(void);		// function to execute on a single keypress
	void	(*paramfunc)(char*);		// function to execute with a typed parameter
	ConsoleHistory *pConsoleHistory;
	ConsoleAutoComplete *pAutoComplete;
} ConsoleDebugMenu;

// menu is an EArray of ConsoleDebugMenu[]s, with terminators
void DoConsoleDebugMenu(ConsoleDebugMenu ***menus);	// run the menu
void ConsoleDebugPause(void);						// often attached to 'p' in menu
ConsoleDebugMenu ***GetDefaultConsoleDebugMenu(void);// Can be appended to by the caller
void ConsoleDebugAddToDefault(ConsoleDebugMenu* menu);

bool ConsoleDebugSomeoneIsTyping(void);

bool isAprilFools(void);
