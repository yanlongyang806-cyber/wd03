//This tiny stupid project exists so that utilitieslib can depend on it, so that structparser can run
//for all projects at the very beginning of the solution compile and

#if _PS3
extern "C" 
void dummyStructParserStub(void) {
}
#else

#if _XBOX
int wmain(int argc_in, char** argv_in)
#else
#include "windows.h"
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, WCHAR*    pWideCmdLine, int nCmdShow)
#endif
{
	return 0;
}

#endif
