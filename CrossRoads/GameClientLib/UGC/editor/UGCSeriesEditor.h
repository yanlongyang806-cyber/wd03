//// Editor for a series of UGC projects.
#pragma once

typedef U32 ContainerID;

typedef struct NOCONST(UGCProjectSeries) NOCONST(UGCProjectSeries);
typedef struct UGCProjectSeries UGCProjectSeries;

void ugcSeriesEditor_EditNewSeries( const UGCProjectSeries* newSeries );
void ugcSeriesEditor_EditSeries( const UGCProjectSeries* series );

void ugcSeriesEditor_ProjectSeriesCreate_Result( ContainerID newSeriesID );
void ugcSeriesEditor_ProjectSeriesUpdate_Result( bool success, const char* errorMsg );
