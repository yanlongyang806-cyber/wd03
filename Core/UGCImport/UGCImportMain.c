#include "UGCExportImport.h"

#include "pyLib.h"
#include "sysutil.h"
#include "gimmeDLLWrapper.h"
#include "file.h"
#include "hoglib.h"
#include "errornet.h"
#include "MemoryMonitor.h"
#include "utilitiesLib.h"
#include "SharedMemory.h"
#include "Organization.h"
#include "pcl_client.h"
#include "ScratchStack.h"
#include "error.h"
#include "FolderCache.h"
#include "cmdparse.h"
#include "Wincon.h"
#include "sock.h"

#include "UGCProjectUtils.h"
#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"

static char s_hoggfilename[MAX_PATH] = "";
AUTO_CMD_STRING(s_hoggfilename, hogg) ACMD_ACCESSLEVEL(0);

static char s_machine[MAX_PATH] = "";
AUTO_CMD_STRING(s_machine, machine) ACMD_ACCESSLEVEL(0);

static char s_username[MAX_PATH] = "";
AUTO_CMD_STRING(s_username, username) ACMD_ACCESSLEVEL(0);

static char s_comment[MAX_PATH] = "";
AUTO_CMD_STRING(s_comment, comment) ACMD_ACCESSLEVEL(0);

static bool s_import_projects = 1;
AUTO_CMD_INT(s_import_projects, import_projects) ACMD_ACCESSLEVEL(0);

static bool s_import_series = 1;
AUTO_CMD_INT(s_import_series, import_series) ACMD_ACCESSLEVEL(0);

static bool s_strip_review_text = 0;
AUTO_CMD_INT(s_strip_review_text, strip_review_text) ACMD_ACCESSLEVEL(0);

static bool s_force_delete = 0;
AUTO_CMD_INT(s_force_delete, force_delete) ACMD_ACCESSLEVEL(0);

static bool s_delete_all_ugc = 0;
AUTO_CMD_INT(s_delete_all_ugc, delete_all_ugc) ACMD_ACCESSLEVEL(0);

int main(int argc, char **argv)
{
	EXCEPTION_HANDLER_BEGIN;

	WAIT_FOR_DEBUGGER;

	setDefaultProductionMode(1);

	DO_AUTO_RUNS

	setDefaultAssertMode();
	gimmeDLLDisable(1);
	hogSetAllowUpgrade(true);

	errorTrackerEnableErrorThreading(false);
	FolderCacheChooseMode();
	memMonitorInit();
	utilitiesLibStartup();
	sharedMemorySetMode(SMM_DISABLED);
	sockStart();

	cmdParseCommandLine(argc, argv);

	if(UGCExportImport_InitPython())
	{
		bool bSuccess = true;
		bool bResume = false;

		int err_return = 0;
		HogFile *hogg = NULL;

		if(0 == strlen(s_hoggfilename) && 0 == strlen(s_machine))
		{
			printf("UGCImport Usage:\n");
			printf("\t-hogg filename\n");
			printf("\t   Specifies the hogg file name to import UGC content from\n");
			printf("\t-machine machinename\n");
			printf("\t   Specifies the machine name running the shard's Server Monitor that should have its UGC projects imported into\n");
			printf("\t-username username\n");
			printf("\t   Specifies the username to use for logging into the machine's Server Monitor\n");
			printf("\t-comment comment\n");
			printf("\t   Specifies the import comment to use. This text is saved in the imported UGCProject container for bookkeeping\n");
			printf("\t-import_projects 1|0\n");
			printf("\t   Determines whether or not to import UGC Projects. Default is 1 (true).\n");
			printf("\t-import_series 1|0\n");
			printf("\t   Determines whether or not to import UGC Project Series. Default is 1 (true).\n");
			printf("\t-strip_review_text 1|0\n");
			printf("\t   Determines if review text should be stripped from imported data. Default is 0 (false).\n");
			printf("\t-force_delete 1|0\n");
			printf("\t   Determines whether or not to force delete any existing UGC Project and UGC Project Series imported previously. Default is 0 (false).\n");
			printf("\t-delete_all_ugc 1|0\n");
			printf("\t   Determines whether or not to delete all existing UGC Accounts, Projects, and Series before importing data from other shard. Default is 0 (false).\n");
			printf("\n");
			printf("Example usage for importing projects and series to UGC shard xorn, which is in the Neverwinter Alpha cluster:\n");
			printf("\n");
			printf("\tUGCImport.exe -machine xorn -username andrewadev -comment \"Importing projects\" -hogg UGCExport.hogg\n");
			return 0;
		}

		if(0 == strlen(s_comment))
		{
			printf("You must specify a comment on the command line using -comment 'Your comment text here'\n");
			return 0;
		}

		hogg = hogFileRead(s_hoggfilename, NULL, PIGERR_PRINTF, &err_return, HOG_NOCREATE|HOG_READONLY);
		if(!hogg)
		{
			printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Couldn't open \"%s\" for reading: %s!\n", s_hoggfilename, hogFileGetArchiveFileName(hogg));
			return 1;
		}

		if(!UGCExportImport_ProxyInit(s_machine, s_username))
			return 1;

		if(!UGCExportImport_Version())
			return 1;

		{
			UGCSearchResult *pUGCSearchResult = StructCreate(parse_UGCSearchResult);
			if(PARSERESULT_ERROR == ParserReadTextFileFromHogg("UGCSearchResult", parse_UGCSearchResult, pUGCSearchResult, hogg))
			{
				printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Could not find UGCSearchResult in hogg %s.\n", hogFileGetArchiveFileName(hogg));
				StructDestroy(parse_UGCSearchResult, pUGCSearchResult);
			}
			else
			{
				UGCPatchInfo *pUGCPatchInfo = StructCreate(parse_UGCPatchInfo);
				if(PARSERESULT_ERROR == ParserReadTextFileFromHogg("UGCPatchInfo", parse_UGCPatchInfo, pUGCPatchInfo, hogg))
				{
					printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Could not find UGCPatchInfo in hogg %s.\n", hogFileGetArchiveFileName(hogg));
					StructDestroySafe(parse_UGCPatchInfo, &pUGCPatchInfo);
				}
				else
				{
					int publishedProjectsImported = 0;
					int savedProjectsImported = 0;
					int projectsErrored = 0;
					int projectsSkipped = 0;
					int projectSeriesImported = 0;
					int projectSeriesErrored = 0;
					int projectSeriesSkipped = 0;
					int index;
					bool bReady = true;

					if(s_import_projects)
						printf("Importing %d UGCProjects from %s\n\n", eaSize(&pUGCSearchResult->eaResults), s_hoggfilename);

					if(s_delete_all_ugc)
						bReady = UGCExportImport_DeleteAllUGC(s_comment);

					if(bReady)
					{
						for(index = 0; index < eaSize(&pUGCSearchResult->eaResults); index++)
						{
							if(s_import_projects && pUGCSearchResult->eaResults[index]->iUGCProjectID)
							{
								UGCProject *pUGCProject = NULL;
								char *estrUGCProjectDataPublished = NULL;
								char *estrUGCProjectDataSaved = NULL;
								char filename[256];

								sprintf(filename, "UGCProject/%d/UGCProject.con", pUGCSearchResult->eaResults[index]->iUGCProjectID);

								pUGCProject = StructCreate(parse_UGCProject);
								if(PARSERESULT_ERROR != ParserReadTextFileFromHogg(filename, parse_UGCProject, pUGCProject, hogg))
								{
									UGCProjectVersion *pUGCProjectVersionPublished = NULL;
									UGCProjectVersion *pUGCProjectVersionSaved = NULL;
									int i;

									for(i = eaSize(&pUGCProject->ppProjectVersions) - 1; i >= 0; i--)
									{
										UGCProjectVersionState eState = ugcProjectGetVersionStateConst(pUGCProject->ppProjectVersions[i]); 
										if(!pUGCProjectVersionPublished &&
												(UGC_PUBLISHED == eState || UGC_REPUBLISHING == eState || UGC_NEEDS_REPUBLISHING == eState))
											pUGCProjectVersionPublished = pUGCProject->ppProjectVersions[i];

										if(!pUGCProjectVersionSaved && eState == UGC_SAVED)
											pUGCProjectVersionSaved = pUGCProject->ppProjectVersions[i];

										if(pUGCProjectVersionPublished && pUGCProjectVersionSaved)
											break;
									}

									if(pUGCProjectVersionPublished || pUGCProjectVersionSaved)
									{
										if(pUGCProjectVersionPublished)
										{
											char *hoggFileName = NULL;
											HogFileIndex hfIndex;
											estrPrintf(&hoggFileName, "data/ns/%s/project/%s", pUGCProjectVersionPublished->pNameSpace, pUGCProject->pIDString);

											hfIndex = hogFileFind(hogg, hoggFileName);
											if(hfIndex != HOG_INVALID_INDEX)
											{
												bool checksum_valid;
												U32 count = 0;
												void *data = hogFileExtract(hogg, hfIndex, &count, &checksum_valid);
												estrCreate(&estrUGCProjectDataPublished);
												estrSetSize(&estrUGCProjectDataPublished, count+1);
												memcpy(estrUGCProjectDataPublished, data, count);
												estrTerminateString(estrFromStr_sekret(estrUGCProjectDataPublished));
												free(data);
											}
											estrDestroy(&hoggFileName);
										}

										if(pUGCProjectVersionSaved)
										{
											char *hoggFileName = NULL;
											HogFileIndex hfIndex;
											estrPrintf(&hoggFileName, "data/ns/%s/project/%s", pUGCProjectVersionSaved->pNameSpace, pUGCProject->pIDString);

											hfIndex = hogFileFind(hogg, hoggFileName);
											if(hfIndex != HOG_INVALID_INDEX)
											{
												bool checksum_valid;
												U32 count = 0;
												void *data = hogFileExtract(hogg, hfIndex, &count, &checksum_valid);
												estrCreate(&estrUGCProjectDataSaved);
												estrSetSize(&estrUGCProjectDataSaved, count+1);
												memcpy(estrUGCProjectDataSaved, data, count);
												estrTerminateString(estrFromStr_sekret(estrUGCProjectDataSaved));
												free(data);
											}
											estrDestroy(&hoggFileName);
										}

										if(estrUGCProjectDataPublished || estrUGCProjectDataSaved)
										{
											// If project has a series ID, make sure the series is in the search result for later importing of it
											if(pUGCProject->seriesID)
											{
												bool bFound = false;
												int j;
												for(j = 0; j < eaSize(&pUGCSearchResult->eaResults); j++)
												{
													if(pUGCSearchResult->eaResults[j]->iUGCProjectSeriesID == pUGCProject->seriesID)
													{
														bFound = true;
														break;
													}
												}
												if(!bFound)
												{
													UGCContentInfo *pUGCContentInfo = StructCreate(parse_UGCContentInfo);
													pUGCContentInfo->iUGCProjectSeriesID = pUGCProject->seriesID;
													eaPush(&pUGCSearchResult->eaResults, pUGCContentInfo);
												}
											}

											// strip review text, if specified
											if(s_strip_review_text)
											{
												NOCONST(UGCProject) *pUGCProjectNoConst = CONTAINER_NOCONST(UGCProject, pUGCProject);
												eaDestroyStructNoConst(&pUGCProjectNoConst->ugcReviews.ppReviews, parse_UGCSingleReview);
											}

											// do the import
											{
												char *result = UGCExportImport_ImportUGCProjectContainerAndData(pUGCProject, estrUGCProjectDataPublished, estrUGCProjectDataSaved, pUGCPatchInfo->shard, s_comment, s_force_delete);
												if(!result || result[0])
												{
													bSuccess = false;
													projectsErrored++;
													printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Failed to import UGC Project %d: %s.\n", pUGCSearchResult->eaResults[index]->iUGCProjectID, result ? result : "UNKNOWN ERROR");
												}
												else
												{
													if(estrUGCProjectDataPublished)
														publishedProjectsImported++;
													else
														savedProjectsImported++;
												}
												if(result)
													PyMem_Free(result);
											}
										}
										else
											projectsSkipped++;

										estrDestroy(&estrUGCProjectDataPublished);
										estrDestroy(&estrUGCProjectDataSaved);
									}
									else
										projectsSkipped++;
								}
								else
									projectsSkipped++;

								StructDestroySafe(parse_UGCProject, &pUGCProject);
							}
							else if(pUGCSearchResult->eaResults[index]->iUGCProjectID)
								projectsSkipped++;

							if(s_import_series && pUGCSearchResult->eaResults[index]->iUGCProjectSeriesID)
							{
								UGCProjectSeries *pUGCProjectSeries = NULL;
								char filename[256];

								sprintf(filename, "UGCProjectSeries/%d/UGCProjectSeries.con", pUGCSearchResult->eaResults[index]->iUGCProjectSeriesID);

								pUGCProjectSeries = StructCreate(parse_UGCProjectSeries);
								if(PARSERESULT_ERROR != ParserReadTextFileFromHogg(filename, parse_UGCProjectSeries, pUGCProjectSeries, hogg))
								{
									// strip review text, if specified
									if(s_strip_review_text)
									{
										int j;
										NOCONST(UGCProjectSeries) *pUGCProjectSeriesNoConst = CONTAINER_NOCONST(UGCProjectSeries, pUGCProjectSeries);
										for(j = 0; j < eaSize(&pUGCProjectSeriesNoConst->ugcReviews.ppReviews); j++)
											SAFE_FREE(pUGCProjectSeriesNoConst->ugcReviews.ppReviews[j]->pComment);
									}

									// do the import
									{
										char *result = UGCExportImport_ImportUGCProjectSeriesContainer(pUGCProjectSeries, pUGCPatchInfo->shard, s_comment, s_force_delete);
										if(!result || result[0])
										{
											bSuccess = false;
											projectSeriesErrored++;
											printfColor(COLOR_RED | COLOR_BRIGHT, "ERROR: Failed to import UGC Project Series %d: %s.\n", pUGCSearchResult->eaResults[index]->iUGCProjectSeriesID, result ? result : "UNKNOWN ERROR");
										}
										else
											projectSeriesImported++;
										if(result)
											PyMem_Free(result);
									}
								}
								else
									projectSeriesSkipped++;

								StructDestroy(parse_UGCProjectSeries, pUGCProjectSeries);
							}
							else if(pUGCSearchResult->eaResults[index]->iUGCProjectSeriesID)
								projectSeriesSkipped++;
						}

						printf("UGC Project Import results: %d published imported, %d saved imported, %d skipped, %d errored\n\n", publishedProjectsImported, savedProjectsImported, projectsSkipped, projectsErrored);
						printf("UGC Series Import results: %d imported, %d skipped, %d errored\n\n", projectSeriesImported, projectSeriesSkipped, projectSeriesErrored);
					}

					StructDestroySafe(parse_UGCPatchInfo, &pUGCPatchInfo);
				}
			}

			StructDestroySafe(parse_UGCSearchResult, &pUGCSearchResult);
		}

		hogFileDestroy(hogg, true);

		return (bSuccess != true); // 0 means no error
	}

	EXCEPTION_HANDLER_END;

	return 1; // couldn't init python
}
