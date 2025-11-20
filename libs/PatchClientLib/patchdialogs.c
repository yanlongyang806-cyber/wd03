#include "patchdialogs_c_ast.h"

AUTO_STRUCT;
typedef struct ProjectEntry
{
	char * Name;
	char * Branch;
	char * Info;
	int branch; NO_AST
	bool no_upload; NO_AST
} ProjectEntry;

AUTO_STRUCT;
typedef struct PatchName
{
	char * Name;
	int Branch;
	char * Sandbox;
} PatchName;

AUTO_STRUCT;
typedef struct GetLatestPath
{
	char * Path;
} GetLatestPath;

#if !PLATFORM_CONSOLE
#include "pcl_client.h"
#include "pcl_client_struct.h"
#include "pcl_client_struct_h_ast.h"
#include "resource.h"
#include "winutil.h"
#include "net/net.h"
#include "ListView.h"
#include "memlog.h"
#include "ScratchStack.h"
#include "timing.h"
#include "UTF8.h"

#include <commctrl.h>

typedef struct LameArrayStruct
{
	const char ** dirs;
	int count;
} LameArrayStruct;

typedef struct LinkDialogStruct
{
	NetComm * comm;
	char connection_status[256];
	char download_feedback[256];
	char root_folder[MAX_PATH];
	char server_address[256];
	int port;
	PCL_Client * client;
	ProjectEntry ** projects;
	ListView * lv_projects;
	UINT_PTR timer_id;
	bool waiting;
	HWND hDlg;
} LinkDialogStruct;

typedef struct RollingProgress
{
	U32 seconds;
	U32 transferred;
} RollingProgress;

typedef struct SyncDialogStruct
{
	NetComm * comm;
	char connection_status[256];
	char download_feedback[256];
	char root_folder[MAX_PATH];
	char link_folder[MAX_PATH];
	ListView * lv_names;
	PatchName ** names;
	char server_address[256];
	char project[256];
	int branch;
	int port;
	bool waiting;
	PCL_Client * client;
	HWND hDlg;
	UINT_PTR timer_id;
	HWND hProgressBar;
	RollingProgress ** rolling_progress;
	bool finished;
} SyncDialogStruct;

typedef struct GetLatestDialogStruct
{
	NetComm * comm;
	char connection_status[256];
	char download_feedback[256];
	char link_folder[MAX_PATH];
	ListView * lv_paths;
	GetLatestPath ** paths;
	char server_address[256];
	char project[256];
	int branch;
	int port;
	bool waiting;
	PCL_Client * client;
	HWND hDlg;
	UINT_PTR timer_id;
	HWND hProgressBar;
	RollingProgress ** rolling_progress;
	bool finished;
} GetLatestDialogStruct;

CRITICAL_SECTION g_patchDialogCritSec;

PCL_ErrorCode pclInitForShellExt(void)
{
	static bool init = false;

	PERFINFO_AUTO_START_FUNC();

	if(!init)
	{
		init = true;
		InitializeCriticalSection(&g_patchDialogCritSec);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return PCL_SUCCESS;
}

static PCL_ErrorCode dialogMessageLoop(HWND dialog)
{
	BOOL ret;
	MSG msg;

	while ((ret = GetMessage(&msg, NULL, 0, 0)) != 0) 
	{ 
		if (ret == -1)
		{
			return PCL_DIALOG_MESSAGE_ERROR;
		}
		else if (!IsWindow(dialog) || !IsDialogMessage(dialog, &msg)) 
		{ 
			TranslateMessage(&msg); 
			DispatchMessage(&msg);
			if(!IsWindow(dialog))
				break;
		}
		Sleep(1);
	}

	return PCL_SUCCESS;
}

static PCL_ErrorCode pclDialogDisconnectSafe(PCL_Client** clientInOut)
{
	PCL_Client* client = SAFE_DEREF(clientInOut);
	
	if(client){
		*clientInOut = NULL;
		return pclDisconnectAndDestroy(client);
	}
	
	return PCL_SUCCESS;
}

static void destroyLinkDialog(LinkDialogStruct * lds)
{
	SetWindowLongPtr(lds->hDlg, GWLP_USERDATA, 0);
	pclDialogDisconnectSafe(&lds->client);
	eaDestroyStruct(&lds->projects, parse_ProjectEntry);
	if(lds->lv_projects)
		listViewDestroy(lds->lv_projects);
	commDestroy(&lds->comm);
	DestroyWindow(lds->hDlg);
	free(lds);
}

static void destroySyncDialog(SyncDialogStruct * sds)
{
	SetWindowLongPtr(sds->hDlg, GWLP_USERDATA, 0);
	pclDialogDisconnectSafe(&sds->client);
	eaDestroyStruct(&sds->names, parse_PatchName);
	if(sds->lv_names)
		listViewDestroy(sds->lv_names);
	eaDestroyEx(&sds->rolling_progress, NULL);
	commDestroy(&sds->comm);
	DestroyWindow(sds->hDlg);
	free(sds);
}

static void destroyGetLatestDialog(GetLatestDialogStruct * glds)
{
	SetWindowLongPtr(glds->hDlg, GWLP_USERDATA, 0);
	pclDialogDisconnectSafe(&glds->client);
	eaDestroyStruct(&glds->paths, parse_GetLatestPath);
	if(glds->lv_paths)
		listViewDestroy(glds->lv_paths);
	eaDestroyEx(&glds->rolling_progress, NULL);
	commDestroy(&glds->comm);
	DestroyWindow(glds->hDlg);
	free(glds);
}

void downloadProgressUpdate(U32 seconds, U32 actual_transferred, U32 total, U32 recieved, U32 total_files, U32 recieved_files, U32 * progress,
							char * feedback, int feedback_size, RollingProgress *** progress_array)
{
	U32 last, time;
	F32 transferred, speed;
	RollingProgress * progress_chunk = malloc(sizeof(RollingProgress));

	progress_chunk->seconds = seconds;
	progress_chunk->transferred = actual_transferred;
	eaPush(progress_array, progress_chunk);
	if(eaSize(progress_array) > 10)
	{
		free((*progress_array)[0]);
		eaRemove(progress_array, 0);
	}

	*progress = (U32)(total ? (recieved * 100.0 / total) : 0);
	last = eaSize(progress_array) - 1;
#pragma warning(suppress:6001) // /analyze "Using uninitialized memory '**progress_array[0]'"
	transferred = (*progress_array)[last]->transferred - (*progress_array)[0]->transferred;
	time = (*progress_array)[last]->seconds - (*progress_array)[0]->seconds;
	speed = ((time > 0) ? (transferred / time) : 0) / (1024.0 * 1024.0);

	sprintf_s(SAFESTR2(feedback), "Network Transfer Rate: %.2f MB/s\nCompleted Files: %u/%u", speed, recieved_files, total_files);
}

bool syncProcessCallback(PatchProcessStats *stats, void *userData)
{
	SyncDialogStruct * sds = userData;

	if(stats->elapsed >= 0.5)
	{
		U32 progress;

		downloadProgressUpdate(stats->seconds, stats->actual_transferred, stats->total, stats->received, stats->total_files,
			stats->received_files, &progress, SAFESTR(sds->download_feedback),
			&sds->rolling_progress);
		if(stats->error)
		{
			char * err = ScratchAlloc(MAX_PATH);

			strcat(sds->download_feedback, " - Error: ");
			pclGetErrorString(stats->error, err, MAX_PATH);
			strcat(sds->download_feedback, err);
			ScratchFree(err);
		}
		SetDlgItemText_UTF8(sds->hDlg, IDC_DOWNLOAD_FEEDBACK, sds->download_feedback);
		SendMessage(sds->hProgressBar, PBM_SETPOS, progress, 0);
		return true;
	}
	else
		return false;
}

bool getLatestProcessCallback(PatchProcessStats *stats, void *userData)
{
	GetLatestDialogStruct * glds = userData;

	if(stats->elapsed >= 0.5)
	{
		U32 progress = 0;

		if(!stats->error)
		{
			downloadProgressUpdate(stats->seconds, stats->actual_transferred, stats->total, stats->received, stats->total_files,
				stats->received_files, &progress,
				SAFESTR(glds->download_feedback), &glds->rolling_progress);
		}
		else
		{
			pclGetErrorString(stats->error, SAFESTR(glds->download_feedback));
		}
		SetDlgItemText_UTF8(glds->hDlg, IDC_DOWNLOAD_FEEDBACK, glds->download_feedback);
		SendMessage(glds->hProgressBar, PBM_SETPOS, progress, 0);
		return true;
	}
	else
		return false;
}

static void linkAfterProjectList(char ** projects, int * max_branch, int * no_upload, int count, PCL_ErrorCode error, const char *error_details, void * userData)
{
	int i, j;
	ProjectEntry * project;
	char branch_name[256];
	LinkDialogStruct * lds = userData;

	lds->waiting = false;

	eaDestroyStruct(&lds->projects, parse_ProjectEntry);

	if(!error)
	{
		for(i = 0; i < count; i++)
		{
			for(j = 1; j <= max_branch[i]; j++)
			{
				sprintf(branch_name, "%i", j);
				project = StructAlloc(parse_ProjectEntry);
				project->Name = StructAllocString(projects[i]);
				project->Branch = StructAllocString(branch_name);
				project->branch = j;
				project->Info = (no_upload[i] ? StructAllocString("Read Only") : StructAllocString("Accepting"));
				project->no_upload = no_upload[i];
				eaPush(&lds->projects, project);
			}

			project = StructAlloc(parse_ProjectEntry);
			project->Name = StructAllocString(projects[i]);
			project->Branch = StructAllocString("Tip");
			project->branch = 0;
			project->Info = (no_upload[i] ? StructAllocString("Read Only") : StructAllocString("Accepting"));
			project->no_upload = no_upload[i];
			eaPush(&lds->projects, project);
		}

		//assertHeapValidateAll();
		listViewDelAllItems(lds->lv_projects, NULL);
		for(i = 0; i < eaSize(&lds->projects); i++)
			listViewAddItem(lds->lv_projects, lds->projects[i]);
		listViewAutosizeColumns(lds->lv_projects);
	}
	else
	{
		strcpy(lds->connection_status, "Error Getting Project List");
		SetDlgItemText_UTF8(lds->hDlg, IDC_CONNECTION_STATUS, lds->connection_status);
	}
}

static void linkAfterConnect(PCL_Client * client, bool updated, PCL_ErrorCode error, const char *error_details, void * userData)
{
	LinkDialogStruct * lds = userData;

	if(!error)
	{
		strcpy(lds->connection_status, "Client Connected");
		SetDlgItemText_UTF8(lds->hDlg, IDC_CONNECTION_STATUS, lds->connection_status);
	}

	error = pclGetProjectList(lds->client, linkAfterProjectList, lds);

	if(error)
	{
		lds->waiting = false;
		strcpy(lds->connection_status, "Error Getting Project List");
		SetDlgItemText_UTF8(lds->hDlg, IDC_CONNECTION_STATUS, lds->connection_status);
	}
}

static void syncAfterGetFiles(PCL_Client * client, PCL_ErrorCode error, const char *error_details, void * userData)
{
	SyncDialogStruct * sds = userData;

	if(!error)
		strcpy(sds->connection_status, "Finished Getting Files");
	else
		error = pclGetErrorString(error, SAFESTR(sds->connection_status));

	SetDlgItemText_UTF8(sds->hDlg, IDC_CONNECTION_STATUS, sds->connection_status);
	sds->waiting = false;
	sds->finished = true;
}

static void syncAfterSetNamedView(PCL_Client * client, PCL_ErrorCode error, const char *error_details, void * userData)
{
	SyncDialogStruct * sds = userData;

	if(!error)
	{
		strcpy(sds->connection_status, "Getting Files...");
		error = pclGetAllFiles(sds->client, syncAfterGetFiles, sds, NULL);
	}
	
	if(error)
	{
		sds->waiting = false;
		error = pclGetErrorString(error, SAFESTR(sds->connection_status));
	}

	SetDlgItemText_UTF8(sds->hDlg, IDC_CONNECTION_STATUS, sds->connection_status);
}

static void syncAfterGetNameList(char ** names, int * branches, char ** sandboxes, U32 * revs, char ** comments, U32 * expires, int count,
	PCL_ErrorCode error, const char *error_details, void * userData)
{
	int i;
	PatchName * name;
	SyncDialogStruct * sds = userData;

	sds->waiting = false;
	if(!error)
	{
		strcpy(sds->connection_status, "Client Connected");
		for(i = 0; i < count; i++)
		{
			name = StructAlloc(parse_PatchName);
			name->Name = StructAllocString(names[i]);
			name->Branch = branches[i];
			name->Sandbox = StructAllocString(sandboxes[i]);
			eaPush(&sds->names, name);
		}

		listViewDelAllItems(sds->lv_names, NULL);
		for(i = 0; i < eaSize(&sds->names); i++)
			listViewAddItem(sds->lv_names, sds->names[i]);
		listViewAutosizeColumns(sds->lv_names);
	}
	else
	{
		error = pclGetErrorString(error, SAFESTR(sds->connection_status));
	}

	SetDlgItemText_UTF8(sds->hDlg, IDC_CONNECTION_STATUS, sds->connection_status);
}

void syncAfterSetView(PCL_Client * client,PCL_ErrorCode error, const char *error_details, void * userData)
{ 
	SyncDialogStruct * sds = userData;

	if(!error)
	{
		strcpy(sds->connection_status, "Getting the Name List");
		error = pclGetNameList(sds->client, syncAfterGetNameList, sds);
	}

	if(error)
	{
		sds->waiting = false;
		error = pclGetErrorString(error, SAFESTR(sds->connection_status));
	}

	SetDlgItemText_UTF8(sds->hDlg, IDC_CONNECTION_STATUS, sds->connection_status);
}

static void syncAfterConnection(PCL_Client * client, bool updated, PCL_ErrorCode error, const char *error_details, void * userData)
{
	SyncDialogStruct * sds = userData;

	if(!error)
	{
		error = pclSetViewLatest(sds->client, sds->project, sds->branch, "", false, false, syncAfterSetView, sds);
	}

	if(error)
	{
		sds->waiting = false;
		error = pclGetErrorString(error, SAFESTR(sds->connection_status));
	}
	else
	{
		strcpy(sds->connection_status, "Setting the Branch");
	}

	SetDlgItemText_UTF8(sds->hDlg, IDC_CONNECTION_STATUS, sds->connection_status);
}

void getLatestAfterSetView(PCL_Client * client, PCL_ErrorCode error, const char *error_details, void * userData)
{
	GetLatestDialogStruct * glds = userData;

	if(!error)
	{
		glds->waiting = false;
		strcpy(glds->connection_status, "Connected to Server");
	}
	else
	{
		error = pclGetErrorString(error, SAFESTR(glds->connection_status));
	}

	SetDlgItemText_UTF8(glds->hDlg, IDC_CONNECTION_STATUS, glds->connection_status);
}

static void getLatestAfterGet(PCL_Client * client, PCL_ErrorCode error, const char *error_details, void * userData, const char * const * filenames)
{
	GetLatestDialogStruct * glds = userData;

	if(!error)
	{
		strcpy(glds->connection_status, "Finished Getting Files");
		SetDlgItemText_UTF8(glds->hDlg, IDC_CONNECTION_STATUS, glds->connection_status);
	}

	if(error)
	{
		error = pclGetErrorString(error, SAFESTR(glds->connection_status));
	}

	glds->waiting = false;
	if(!error)
		glds->finished = true;
}

static void getLatestAfterConnection(PCL_Client * client, bool updated, PCL_ErrorCode error, const char *error_details, void * userData)
{
	GetLatestDialogStruct * glds = userData;

	pclSetFileFlags(glds->client, PCL_SET_GIMME_STYLE);

	if(!error)
	{
		error = pclSetViewLatest(glds->client, glds->project, glds->branch, "", true, true, getLatestAfterSetView, glds);
	}
	if(!error)
	{
		strcpy(glds->connection_status, "Getting Latest View");
	}

	if(error)
	{
		glds->waiting = false;
		error = pclGetErrorString(error, SAFESTR(glds->connection_status));
	}

	SetDlgItemText_UTF8(glds->hDlg, IDC_CONNECTION_STATUS, glds->connection_status);
}

static void linkConnectToServer(LinkDialogStruct * lds)
{
	char * found;
	PCL_ErrorCode error;
	char *pTempEString = NULL;

	lds->waiting = false;
	strcpy(lds->connection_status, "Attempting to Connect");
	SetDlgItemText_UTF8(lds->hDlg, IDC_CONNECTION_STATUS, lds->connection_status);

	estrStackCreate(&pTempEString);
	GetDlgItemText_UTF8(lds->hDlg, IDC_EDIT_SERVER, &pTempEString);
	strcpy_trunc(lds->server_address, pTempEString);
	estrDestroy(&pTempEString);

	found = strchr(lds->server_address, ':');
	if(found)
	{
		found[0] = '\0';
		++found;
		sscanf(found, "%i", &lds->port);
	}
	else
	{
		lds->port = DEFAULT_PATCHSERVER_PORT;
	}

	error = pclDialogDisconnectSafe(&lds->client);
	if(!error)
	{
		error = pclConnectAndCreate(&lds->client,
									lds->server_address,
									lds->port,
									60.0*2,
									lds->comm,
									lds->root_folder,
									NULL,
									NULL,
									linkAfterConnect,
									lds);
	}
	
	if(!error)
	{
		lds->waiting = true;
	}
	else
	{
		error = pclGetErrorString(error, SAFESTR(lds->connection_status));
		SetDlgItemText_UTF8(lds->hDlg, IDC_CONNECTION_STATUS, lds->connection_status);
	}
}

LRESULT CALLBACK linkDialogProcedure(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	int i, count;
	HWND dlg_item;
	HICON icon;
	ProjectEntry * proj;
	BOOL ret = FALSE;
	char link_dir[MAX_PATH];
	int error;
#pragma warning(push)
#pragma warning(disable:4312)
	LinkDialogStruct * lds = (LinkDialogStruct*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
#pragma warning(pop)

	if(iMsg != WM_INITDIALOG && !lds)
		return FALSE;

	EnterCriticalSection(&g_patchDialogCritSec);

	switch(iMsg)
	{
	case WM_TIMER:
		if(lds && lds->waiting)
		{
			if(lds->comm)
				commMonitor(lds->comm);
			if(lds->client)
				pclProcessTracked(lds->client);
		}
		break;
	case WM_INITDIALOG:
		lds = calloc(1, sizeof(LinkDialogStruct));
		lds->comm = commCreate(1, 1);
		lds->timer_id = SetTimer(hDlg, 0, 10, NULL);
		strcpy(lds->connection_status, "Not Connected");
		lds->lv_projects = listViewCreate();
		lds->hDlg = hDlg;

		strcpy(lds->root_folder, (char *)lParam);
		forwardSlashes(lds->root_folder);
		if(strEndsWith(lds->root_folder, "/"))
		{
			lds->root_folder[strlen(lds->root_folder) - 1] = '\0';
		}
		error = pclCheckCurrentLink(lds->root_folder, NULL, 0, NULL, NULL, 0, NULL, NULL, SAFESTR(link_dir));
		if(!error)
		{
			strcpy(lds->root_folder, link_dir);
		}

		SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)lds);

		dlg_item = GetDlgItem(hDlg, IDC_PROJECT_LIST);
		listViewInit(lds->lv_projects, parse_ProjectEntry, hDlg, dlg_item);
		listViewAutosizeColumns(lds->lv_projects);

		lds->download_feedback[0] = '\0';

		SetDlgItemText_UTF8(hDlg, IDC_CONNECTION_STATUS, lds->connection_status);
		SetDlgItemText_UTF8(hDlg, IDC_DOWNLOAD_FEEDBACK, lds->download_feedback);
		icon = LoadIcon(winGetHInstance(), MAKEINTRESOURCE(IDI_PATCH_ICON));
		SendMessage(hDlg, WM_SETICON, (WPARAM) ICON_SMALL, (LPARAM) icon);
		ShowWindow(hDlg, SW_SHOW);
		break;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDLINK:
			if(!lds->waiting)
			{
				count = 0;
				for(i = 0; i < eaSize(&lds->projects); i++)
				{
					if(listViewIsSelected(lds->lv_projects, lds->projects[i]))
					{
						count++;
						proj = lds->projects[i];
					}
				}
				if(count == 1)
				{
					char dir[MAX_PATH];
					LinkInfo link_info = {0};

					sprintf(dir, "%s/%s/%s", lds->root_folder, PATCH_DIR, CONNECTION_FILE_NAME);
					makeDirectoriesForFile(dir);
					link_info.ip = lds->server_address;
					link_info.no_upload = proj->no_upload;
					link_info.port = lds->port;
					link_info.project = proj->Name;
					link_info.branch = proj->branch;
					ParserWriteTextFile(dir, parse_LinkInfo, &link_info, 0, 0);

					destroyLinkDialog(lds);
					ret = TRUE;
				}
				else if(eaSize(&lds->projects) == 0)
				{
					linkConnectToServer(lds);
				}
			}
			break;
		case IDCANCEL:
			destroyLinkDialog(lds);
			ret = TRUE;
			break;
		case IDC_GET_PROJECTS:
			linkConnectToServer(lds);
			break;
		}
		break;
	case WM_NOTIFY:
		listViewOnNotify(lds->lv_projects, wParam, lParam, NULL);
		break;
	}

	LeaveCriticalSection(&g_patchDialogCritSec);

	return ret;
}

LRESULT CALLBACK syncDialogProcedure(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	int i, count;
	HWND dlg_item;
	HICON icon;
	PatchName * name;
	PCL_ErrorCode error;
	BOOL ret = FALSE;
#pragma warning(push)
#pragma warning(disable:4312)
	SyncDialogStruct * sds = (SyncDialogStruct*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
#pragma warning(pop)

	if(iMsg != WM_INITDIALOG && !sds)
		return FALSE;

	EnterCriticalSection(&g_patchDialogCritSec);

	switch(iMsg)
	{
	case WM_TIMER:
		if(sds->waiting)
		{
			if(sds->comm)
				commMonitor(sds->comm);
			if(sds->client)
				pclProcessTracked(sds->client);
		}
		if(sds->finished)
		{
			destroySyncDialog(sds);
			ret = TRUE;
		}
		break;
	case WM_INITDIALOG:
		sds = calloc(1, sizeof(SyncDialogStruct));
		strcpy(sds->connection_status, "Attempting to Connect");
		sds->comm = commCreate(1, 1);
		sds->timer_id = SetTimer(hDlg, 0, 10, NULL);
		sds->hDlg = hDlg;
		sds->lv_names = listViewCreate();
		sds->waiting = true;
		sds->hProgressBar = GetDlgItem(hDlg, IDC_PROGRESS); 

		strcpy(sds->root_folder, (char *)lParam);
		forwardSlashes(sds->root_folder);
		if(strEndsWith(sds->root_folder, "/"))
		{
			sds->root_folder[strlen(sds->root_folder) - 1] = '\0';
		}

		SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)sds);

		dlg_item = GetDlgItem(hDlg, IDC_NAME_LIST);
		listViewInit(sds->lv_names, parse_PatchName, hDlg, dlg_item);
		listViewAutosizeColumns(sds->lv_names);

		error = pclCheckCurrentLink(sds->root_folder, SAFESTR(sds->server_address), &sds->port, SAFESTR(sds->project),
									&sds->branch, NULL, SAFESTR(sds->link_folder));
		if(!error)
		{
			error = pclConnectAndCreate(&sds->client,
										sds->server_address,
										sds->port,
										60.0*2,
										sds->comm,
										sds->link_folder,
										NULL,
										NULL,
										syncAfterConnection,
										sds);
		}
		
		if(!error)
			error = pclSetFileFlags(sds->client, PCL_SET_GIMME_STYLE);
		if(!error)
			error = pclSetProcessCallback(sds->client, syncProcessCallback, sds);

		if(error || !sds->client)
		{
			sds->waiting = false;
			error = pclGetErrorString(error, SAFESTR(sds->connection_status));
		}

		sds->download_feedback[0] = '\0';

		SendMessage(sds->hProgressBar, PBM_SETPOS, (WPARAM) 0, 0);
		icon = LoadIcon(winGetHInstance(), MAKEINTRESOURCE(IDI_PATCH_ICON));
		SendMessage(hDlg, WM_SETICON, (WPARAM) ICON_SMALL, (LPARAM) icon);
		SetDlgItemText_UTF8(hDlg, IDC_CONNECTION_STATUS, sds->connection_status);
		SetDlgItemText_UTF8(hDlg, IDC_DOWNLOAD_FEEDBACK, sds->download_feedback);
		ShowWindow(hDlg, SW_SHOW);
		break;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDCANCEL:
			destroySyncDialog(sds);
			ret = TRUE;
			break;
		case IDSYNCTONAME:
			if(!sds->waiting)
			{
				count = 0;
				for(i = 0; i < eaSize(&sds->names); i++)
				{
					if(listViewIsSelected(sds->lv_names, sds->names[i]))
					{
						count++;
						name = sds->names[i];
					}
				}
				if(count == 1)
				{
					error = pclSetNamedView(sds->client, sds->project, name->Name, true, true, syncAfterSetNamedView, sds);
					if(!error)
					{
						sds->waiting = true;
						strcpy(sds->connection_status, "Setting Named View");
					}
					else
					{
						error = pclGetErrorString(error, SAFESTR(sds->connection_status));
					}
					SetDlgItemText_UTF8(hDlg, IDC_CONNECTION_STATUS, sds->connection_status);
				}
			}
			break;
		}
		break;
	case WM_NOTIFY:
		listViewOnNotify(sds->lv_names, wParam, lParam, NULL);
		break;
	}

	LeaveCriticalSection(&g_patchDialogCritSec);

	return ret;
}

LRESULT CALLBACK getLatestDialogProcedure(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	int i;
	HWND dlg_item;
	HICON icon;
	BOOL ret = FALSE;
	PCL_ErrorCode error;
	LameArrayStruct * input_dirs = (LameArrayStruct*) lParam;
#pragma warning(push)
#pragma warning(disable:4312)
	GetLatestDialogStruct * glds = (GetLatestDialogStruct*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
#pragma warning(pop)

	if(iMsg != WM_INITDIALOG && !glds)
		return FALSE;
	
	switch(iMsg)
	{
	case WM_TIMER:
		if(glds->waiting)
		{
			if(glds->comm)
				commMonitor(glds->comm);
			if(glds->client)
				pclProcessTracked(glds->client);
		}
		if(glds->finished)
		{
			destroyGetLatestDialog(glds);
			ret = TRUE;
		}
		break;
	case WM_INITDIALOG:
		glds = calloc(1, sizeof(SyncDialogStruct));
		strcpy(glds->connection_status, "Attempting to Connect");
		glds->comm = commCreate(1, 1);
		glds->timer_id = SetTimer(hDlg, 0, 10, NULL);
		glds->hDlg = hDlg;
		glds->lv_paths = listViewCreate();
		glds->waiting = true;
		glds->hProgressBar = GetDlgItem(hDlg, IDC_PROGRESS); 

		for(i = 0; i < input_dirs->count; i++)
		{
			GetLatestPath * path = StructAlloc(parse_GetLatestPath);
			path->Path = StructAllocString(input_dirs->dirs[i]);
			forwardSlashes(path->Path);
			if(path->Path[strlen(path->Path) - 1] == '/')
				path->Path[strlen(path->Path) - 1] = '\0';
			eaPush(&glds->paths, path);
		}

		SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)glds);

		dlg_item = GetDlgItem(hDlg, IDC_PATH_LIST);
		listViewInit(glds->lv_paths, parse_GetLatestPath, hDlg, dlg_item);

		if(eaSize(&glds->paths) > 0)
		{
			error = pclCheckCurrentLink(glds->paths[0]->Path, SAFESTR(glds->server_address), &glds->port, SAFESTR(glds->project), &glds->branch, NULL,
				SAFESTR(glds->link_folder));
			if(error)
			{
				error = pclGetErrorString(error, SAFESTR(glds->connection_status));
			}
			else
			{
				error = pclConnectAndCreate(&glds->client,
											glds->server_address,
											glds->port,
											60.0*2,
											glds->comm,
											glds->link_folder,
											NULL,
											NULL,
											getLatestAfterConnection,
											glds);

				if(!glds->client)
				{
					glds->waiting = false;
					error = pclGetErrorString(error, SAFESTR(glds->connection_status));
				}
				else
				{
					pclSetFileFlags(glds->client, PCL_SET_GIMME_STYLE);
					pclSetProcessCallback(glds->client, getLatestProcessCallback, glds);
				}
			}
		}
		else
		{
			strcpy(glds->connection_status, "Error Finding a Directory");
		}

		listViewEnableCheckBoxes(glds->lv_paths, true);
		for(i = 0; i < eaSize(&glds->paths); i++)
		{
			listViewAddItem(glds->lv_paths, glds->paths[i]);
			listViewSetChecked(glds->lv_paths, glds->paths[i], true);
		}
		listViewAutosizeColumns(glds->lv_paths);
		
		glds->download_feedback[0] = '\0';

		SendMessage(glds->hProgressBar, PBM_SETPOS, (WPARAM) 0, 0);
		icon = LoadIcon(winGetHInstance(), MAKEINTRESOURCE(IDI_PATCH_ICON));
		SendMessage(hDlg, WM_SETICON, (WPARAM) ICON_SMALL, (LPARAM) icon);
		SetDlgItemText_UTF8(hDlg, IDC_CONNECTION_STATUS, glds->connection_status);
		SetDlgItemText_UTF8(hDlg, IDC_DOWNLOAD_FEEDBACK, glds->download_feedback);
		ShowWindow(hDlg, SW_SHOW);
		break;
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDCANCEL:
			destroyGetLatestDialog(glds);
			ret = TRUE;
			break;
		case IDGETLATEST:
			if(!glds->waiting)
			{
				char ** file_list = NULL;
				int * recurse = NULL;
				
				for(i = 0; i < eaSize(&glds->paths); i++)
				{
					eaPush(&file_list, glds->paths[i]->Path);
					eaiPush(&recurse, 1);
				}
				error = pclGetFileList(	glds->client,
										file_list,
										recurse,
										false,
										eaSize(&file_list),
										getLatestAfterGet,
										glds,
										NULL);
				if(error)
				{
					error = pclGetErrorString(error, SAFESTR(glds->connection_status));
				}
				else
				{
					strcpy(glds->connection_status, "Getting Files");
					glds->waiting = true;
				}
				eaDestroy(&file_list);
				eaiDestroy(&recurse);
				SetDlgItemText_UTF8(hDlg, IDC_CONNECTION_STATUS, glds->connection_status);
			}
			break;
		}
		break;
	case WM_NOTIFY:
		listViewOnNotify(glds->lv_paths, wParam, lParam, NULL);
		break;
	}

	return ret;
}

PCL_ErrorCode pclLinkDialog(const char * dir, bool message_loop)
{
	HWND link_dialog;
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	memlog_printf(NULL, "Entering pclLinkDialog\n");

	link_dialog = CreateDialogParam(winGetHInstance(), (LPCTSTR) IDD_DIALOG_LINK, NULL, (DLGPROC) linkDialogProcedure, (LPARAM) dir);

	if(message_loop)
		error = dialogMessageLoop(link_dialog);
	else
		error = PCL_SUCCESS;

	PERFINFO_AUTO_STOP_FUNC();

	return error;
}

PCL_ErrorCode pclSyncDialog(const char * dir, bool message_loop)
{
	HWND sync_dialog;
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	memlog_printf(NULL, "Entering pclSyncDialog\n");

	sync_dialog = CreateDialogParam(winGetHInstance(), (LPCTSTR) IDD_DIALOG_SYNC, NULL, (DLGPROC) syncDialogProcedure, (LPARAM) dir);

	if(message_loop)
		error = dialogMessageLoop(sync_dialog);
	else
		error = PCL_SUCCESS;

	PERFINFO_AUTO_STOP_FUNC();

	return error;
}

PCL_ErrorCode pclGetLatestDialog(const char ** dirs, int count, bool message_loop)
{
	HWND get_latest_dialog;
	LameArrayStruct param = {0};
	PCL_ErrorCode error;

	PERFINFO_AUTO_START_FUNC();

	memlog_printf(NULL, "Entering pclGetLatestDialog\n");

	param.count = count;
	param.dirs = dirs;
	get_latest_dialog = CreateDialogParam(winGetHInstance(), MAKEINTRESOURCE(IDD_DIALOG_GET_LATEST), NULL, (DLGPROC) getLatestDialogProcedure,
										  (LPARAM) &param);

	if(message_loop)
		error = dialogMessageLoop(get_latest_dialog);
	else
		error = PCL_SUCCESS;

	PERFINFO_AUTO_STOP_FUNC();

	return error;
}

PCL_ErrorCode pclMirrorDialog(const char ** dirs, int count, bool message_loop)
{
	//HWND mirror_dialog;
	LameArrayStruct param = {0};

	memlog_printf(NULL, "Entering pclMirrorDialog\n");

	param.count = count;
	param.dirs = dirs;
	/*mirror_dialog = CreateDialogParam(winGetHInstance(), (LCPSTR) IDD_DIALOG_MIRROR, NULL, (DLGPROC) mirrorDialogProcedure,
									  (LPARAM) &param);

	if(message_loop)
		return dialogMessageLoop(mirror_dialog);
	else*/
		return PCL_SUCCESS;
}

PCL_ErrorCode pclUnmirrorDialog(const char ** dirs, int count, bool message_loop)
{
	//HWND unmirror_dialog;
	LameArrayStruct param = {0};

	memlog_printf(NULL, "Entering pclMirrorDialog\n");

	param.count = count;
	param.dirs = dirs;
	/*unmirror_dialog = CreateDialogParam(winGetHInstance(), (LCPSTR) IDD_DIALOG_UNMIRROR, NULL, (DLGPROC) unmirrorDialogProcedure,
		(LPARAM) &param);

	if(message_loop)
		return dialogMessageLoop(unmirror_dialog);
	else*/
		return PCL_SUCCESS;
}

#endif

#include "patchdialogs_c_ast.c"