//// A bunch of utility function that are heavily tied to headshots.
#include"GfxHeadshot.h"
#include"GfxHeadshot_util_c_ast.h"

#include"GfxDebug.h"
#include"WorldGrid.h"
#include"ObjectLibrary.h"
#include "Color.h"

AUTO_RUN_ANON(memBudgetAddMapping( __FILE__, BUDGET_EngineMisc ););

/// A collection of headshots to save out.
AUTO_STRUCT;
typedef struct HeadshotList
{
	char** groupName;
} HeadshotList;

/// Save a collection of GroupDefs, as described in GROUP-LIST-FILE
/// out to the screenshots directory.  The headshots will be of size
/// WIDTH x HEIGHT.
///
/// Screeshots will be named groups/<GROUP-NAME>.jpg
AUTO_COMMAND;
void headshotSaveScreenshots( const char* listFile, int width, int height )
{
	HeadshotList list;
	StructInit( parse_HeadshotList, &list );
	
	if( !fileExists( listFile )) {
		Alertf( "Could not find file \"%s\".", listFile );
		return;
	}
	if( !ParserReadTextFile( listFile, parse_HeadshotList, &list, 0 )) {
		Alertf( "Could not read file \"%s\", bad syntax?", listFile );
		return;
	}

	gfxShowPleaseWaitMessage( "Saving headshots..." );
	{
		int it;
		for( it = 0; it != eaSize( &list.groupName ); ++it ) {
			char* groupName = list.groupName[ it ];
			GroupDef* group = objectLibraryGetGroupDefByName( groupName, true );
			
			char destBuffer[ MAX_PATH ];
			sprintf( destBuffer, "groups/%s.jpg", groupName );
			gfxHeadshotCaptureGroupAndSave( destBuffer, width, height, group, NULL, ColorGray, NULL, NULL );
		}
	}

	StructDeInit( parse_HeadshotList, &list );
}

#include "GfxHeadshot_util_c_ast.c"
