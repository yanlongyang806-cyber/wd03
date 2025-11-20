
#include "mastercontrolprogram.h"
#include "MCPGimmeCheckins.h"
#include "MCPSVNCheckins.h"
#include "MCPPurgeLogFiles.h"
#include "MCPXboxCP.h"
#include "MCPMemLeakFinder.h"
#include "earray.h"
#include "winutil.h"
#include "GlobalTypes.h"
#include "winInclude.h"
#include "Windowsx.h"
#include "estring.h"
#include "qsortg.h"

static U32 *spServerTypesToConfig = NULL;

U32 sServerNameIDs[] = 
{
	IDC_CONFIGSERVERNAME1,
	IDC_CONFIGSERVERNAME2,
	IDC_CONFIGSERVERNAME3,
	IDC_CONFIGSERVERNAME4,
	IDC_CONFIGSERVERNAME5,
	IDC_CONFIGSERVERNAME6,
	IDC_CONFIGSERVERNAME7,
	IDC_CONFIGSERVERNAME8,
	IDC_CONFIGSERVERNAME9,
	IDC_CONFIGSERVERNAME10,
	IDC_CONFIGSERVERNAME11,
	IDC_CONFIGSERVERNAME12,
	IDC_CONFIGSERVERNAME13,
	IDC_CONFIGSERVERNAME14,
	IDC_CONFIGSERVERNAME15,
	IDC_CONFIGSERVERNAME16,
	IDC_CONFIGSERVERNAME17,
	IDC_CONFIGSERVERNAME18,
	IDC_CONFIGSERVERNAME19,
	IDC_CONFIGSERVERNAME20,
	IDC_CONFIGSERVERNAME21,
	IDC_CONFIGSERVERNAME22,
	IDC_CONFIGSERVERNAME23,
	IDC_CONFIGSERVERNAME24,
	IDC_CONFIGSERVERNAME25,
	IDC_CONFIGSERVERNAME26,
	IDC_CONFIGSERVERNAME27,
	IDC_CONFIGSERVERNAME28,
	IDC_CONFIGSERVERNAME29,
	IDC_CONFIGSERVERNAME30,
	IDC_CONFIGSERVERNAME31,
	IDC_CONFIGSERVERNAME32,
	IDC_CONFIGSERVERNAME33,
	IDC_CONFIGSERVERNAME34,
};

U32 sForceVisibleIDs[] = 
{
	IDC_FORCE_VISIBLE1,
	IDC_FORCE_VISIBLE2,
	IDC_FORCE_VISIBLE3,
	IDC_FORCE_VISIBLE4,
	IDC_FORCE_VISIBLE5,
	IDC_FORCE_VISIBLE6,
	IDC_FORCE_VISIBLE7,
	IDC_FORCE_VISIBLE8,
	IDC_FORCE_VISIBLE9,
	IDC_FORCE_VISIBLE10,
	IDC_FORCE_VISIBLE11,
	IDC_FORCE_VISIBLE12,
	IDC_FORCE_VISIBLE13,
	IDC_FORCE_VISIBLE14,
	IDC_FORCE_VISIBLE15,
	IDC_FORCE_VISIBLE16,
	IDC_FORCE_VISIBLE17,
	IDC_FORCE_VISIBLE18,
	IDC_FORCE_VISIBLE19,
	IDC_FORCE_VISIBLE20,
	IDC_FORCE_VISIBLE21,
	IDC_FORCE_VISIBLE22,
	IDC_FORCE_VISIBLE23,
	IDC_FORCE_VISIBLE24,
	IDC_FORCE_VISIBLE25,
	IDC_FORCE_VISIBLE26,
	IDC_FORCE_VISIBLE27,
	IDC_FORCE_VISIBLE28,
	IDC_FORCE_VISIBLE29,
	IDC_FORCE_VISIBLE30,
	IDC_FORCE_VISIBLE31,
	IDC_FORCE_VISIBLE32,
	IDC_FORCE_VISIBLE33,
	IDC_FORCE_VISIBLE34,
};


U32 sForceHiddenIDs[] = 
{
	IDC_FORCE_HIDDEN1,
	IDC_FORCE_HIDDEN2,
	IDC_FORCE_HIDDEN3,
	IDC_FORCE_HIDDEN4,
	IDC_FORCE_HIDDEN5,
	IDC_FORCE_HIDDEN6,
	IDC_FORCE_HIDDEN7,
	IDC_FORCE_HIDDEN8,
	IDC_FORCE_HIDDEN9,
	IDC_FORCE_HIDDEN10,
	IDC_FORCE_HIDDEN11,
	IDC_FORCE_HIDDEN12,
	IDC_FORCE_HIDDEN13,
	IDC_FORCE_HIDDEN14,
	IDC_FORCE_HIDDEN15,
	IDC_FORCE_HIDDEN16,
	IDC_FORCE_HIDDEN17,
	IDC_FORCE_HIDDEN18,
	IDC_FORCE_HIDDEN19,
	IDC_FORCE_HIDDEN20,
	IDC_FORCE_HIDDEN21,
	IDC_FORCE_HIDDEN22,
	IDC_FORCE_HIDDEN23,
	IDC_FORCE_HIDDEN24,
	IDC_FORCE_HIDDEN25,
	IDC_FORCE_HIDDEN26,
	IDC_FORCE_HIDDEN27,
	IDC_FORCE_HIDDEN28,
	IDC_FORCE_HIDDEN29,
	IDC_FORCE_HIDDEN30,
	IDC_FORCE_HIDDEN31,
	IDC_FORCE_HIDDEN32,
	IDC_FORCE_HIDDEN33,
	IDC_FORCE_HIDDEN34,
};

STATIC_ASSERT(sizeof(sServerNameIDs) == sizeof(sForceVisibleIDs));
STATIC_ASSERT(sizeof(sServerNameIDs) == sizeof(sForceHiddenIDs));

BOOL configMenuDlgProc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	ServerTypeConfiguration *pConfig;
	LRESULT lResult;
	int i;

	switch (iMsg)
	{
	case WM_INITDIALOG:
		{
			char *pMinimalServersComment = NULL;
			bool bMinimalFirst = true;

			SetTimer(hDlg, 0, 1, NULL);

			ea32Destroy(&spServerTypesToConfig);

			estrCopy2(&pMinimalServersComment, "Note: the following server types are always present: ");

			for (i=0; i < eaSize(&gGlobalStaticSettings.ppContainerStaticInfo); i++)
			{
				if (gGlobalStaticSettings.ppContainerStaticInfo[i]->bIsInMinimalShard)
				{	
					estrConcatf(&pMinimalServersComment, "%s%s", bMinimalFirst ? "" : ", ", GlobalTypeToName(gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType));
					bMinimalFirst = false;
				}
				else
				{
					ea32Push(&spServerTypesToConfig, gGlobalStaticSettings.ppContainerStaticInfo[i]->eContainerType);
				}
			}

			ea32QSort(spServerTypesToConfig,cmpU32);

			assert(ea32Size(&spServerTypesToConfig) <= ARRAY_SIZE(sServerNameIDs));

			for (i=0; i < ARRAY_SIZE(sServerNameIDs); i++)
			{
				if (i < ea32Size(&spServerTypesToConfig))
				{
					ContainerDynamicInfo *pDynamicInfo = GetContainerDynamicInfoFromType(spServerTypesToConfig[i]);

					ShowWindow(GetDlgItem(hDlg, sForceVisibleIDs[i]), SW_SHOW);
					ShowWindow(GetDlgItem(hDlg, sForceHiddenIDs[i]), SW_SHOW);
					ShowWindow(GetDlgItem(hDlg, sServerNameIDs[i]), SW_SHOW);

					SetTextFast(GetDlgItem(hDlg, sServerNameIDs[i]), GlobalTypeToName(spServerTypesToConfig[i]));


					CheckDlgButton(hDlg, sForceVisibleIDs[i], pDynamicInfo->bForceShowServerType);
					CheckDlgButton(hDlg, sForceHiddenIDs[i], pDynamicInfo->bForceHideServerType);

				}
				else
				{
					ShowWindow(GetDlgItem(hDlg, sForceVisibleIDs[i]), SW_HIDE);
					ShowWindow(GetDlgItem(hDlg, sForceHiddenIDs[i]), SW_HIDE);
					ShowWindow(GetDlgItem(hDlg, sServerNameIDs[i]), SW_HIDE);
				}
			}
			


			SendMessage(GetDlgItem(hDlg, IDC_PICKCONFIG), CB_RESETCONTENT, 0, 0);

			for (i=0; i < eaSize(&gGlobalStaticSettings.ppServerTypeConfigurations); i++)
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_PICKCONFIG), CB_ADDSTRING, 0, (LPARAM)gGlobalStaticSettings.ppServerTypeConfigurations[i]->pName);
				SendMessage(GetDlgItem(hDlg, IDC_PICKCONFIG), CB_SETITEMDATA, (WPARAM)lResult, (LPARAM)i);
			}

			SendMessage(GetDlgItem(hDlg, IDC_PICKCONFIG), CB_SELECTSTRING, 0, (LPARAM)gGlobalDynamicSettings.serverTypeConfig);

			pConfig = FindServerTypeConfig(gGlobalDynamicSettings.serverTypeConfig);

			
			SetTextFast(GetDlgItem(hDlg, IDC_CONFIG_COMMENT), pConfig && pConfig->pComment ? pConfig->pComment : "");

			SetTextFast(GetDlgItem(hDlg, IDC_MINIMAL_SERVERS), pMinimalServersComment);

			estrDestroy(&pMinimalServersComment);
		}
		break;

	case WM_TIMER:
		lResult = SendMessage(GetDlgItem(hDlg, IDC_PICKCONFIG), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
		if (lResult != CB_ERR) 
		{
			lResult = SendMessage(GetDlgItem(hDlg, IDC_PICKCONFIG), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
			if (lResult != CB_ERR) 
			{

				pConfig = gGlobalStaticSettings.ppServerTypeConfigurations[lResult];
				SetTextFast(GetDlgItem(hDlg, IDC_CONFIG_COMMENT), pConfig->pComment ? pConfig->pComment : "");

				for (i = 0; i < ea32Size(&spServerTypesToConfig); i++)
				{
					bool bEnable;

					if (IsDlgButtonChecked(hDlg, sForceVisibleIDs[i]))
					{
						bEnable = true;
					}
					else if (IsDlgButtonChecked(hDlg, sForceHiddenIDs[i]))
					{
						bEnable = false;
					}
					else
					{
						bEnable = (ea32Find(&pConfig->pFixedUpTypes, spServerTypesToConfig[i]) != -1);
					}

					Static_Enable(GetDlgItem(hDlg, sServerNameIDs[i]), bEnable);
					
				}
			}
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
			pWindow->bCloseRequested = true;

			return FALSE;

		case IDOK:
			{
				lResult = SendMessage(GetDlgItem(hDlg, IDC_PICKCONFIG), CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
				if (lResult != CB_ERR) 
				{

					lResult = SendMessage(GetDlgItem(hDlg, IDC_PICKCONFIG), CB_GETITEMDATA, (WPARAM)lResult, (LPARAM)0);
					if (lResult != CB_ERR) 
					{

						strcpy(gGlobalDynamicSettings.serverTypeConfig, gGlobalStaticSettings.ppServerTypeConfigurations[lResult]->pName);
					}
				}

				for (i = 0; i < ea32Size(&spServerTypesToConfig); i++)
				{
					ContainerDynamicInfo *pDynamicInfo = GetContainerDynamicInfoFromType(spServerTypesToConfig[i]);

					pDynamicInfo->bForceShowServerType = IsDlgButtonChecked(hDlg, sForceVisibleIDs[i]);
					pDynamicInfo->bForceHideServerType = IsDlgButtonChecked(hDlg, sForceHiddenIDs[i]);
				}

				WriteSettingsToFile();
				pWindow->bCloseRequested = true;
			}

			return FALSE;
		default:
			for (i = 0; i < ea32Size(&spServerTypesToConfig); i++)
			{
				if (LOWORD(wParam) == sForceVisibleIDs[i])
				{
					CheckDlgButton(hDlg, sForceHiddenIDs[i], 0);
					return FALSE;
				}

				if (LOWORD(wParam) == sForceHiddenIDs[i])
				{
					CheckDlgButton(hDlg, sForceVisibleIDs[i], 0);
					return FALSE;
				}
			}
		}
	}
	
	return FALSE;
}
