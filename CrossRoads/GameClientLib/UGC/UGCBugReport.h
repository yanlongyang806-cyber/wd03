//// Interface for a UI2Lib version of the bug button.
////
//// The UIGen code is in gclBugReport.c, this is a minimal, focused
//// copy of it.

void ugc_ReportBug( void );
void ugc_ReportBugOncePerFrame( void );
void ugc_ReportBugSearchResult( const char* ticketResponse );
