#include "GraphicsLib.h"
#include "UIInternal.h"
#include "earray.h"
#include "UIAuxDevice.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static S32 s_iDevice = 1;

typedef struct UIAuxDeviceData
{
	GfxAuxDeviceCloseCallback cbClose;
	UserData pCloseData;
} UIAuxDeviceData;

static void AuxDevicePerFrameCallback(RdrDevice *pDevice, UIAuxDeviceData *pData)
{
	gfxAuxDeviceDefaultTop(pDevice, 0, ui_OncePerFramePerDevice);
	gfxAuxDeviceDefaultBottom(pDevice, 0);
}

static bool AuxDeviceClose(RdrDevice *pDevice, UIAuxDeviceData *pData)
{
	if (pData && pData->cbClose && !pData->cbClose(pDevice, pData->pCloseData))
		return false;
	ui_StateFreeForDevice(pDevice);
	gfxAuxDeviceRemove(pDevice);
	SAFE_FREE(pData);
	return true;
}

bool ui_AuxDeviceMoveWidgets(RdrDevice *pDevice, UserData pData)
{
	UIDeviceState *pState = ui_StateForDevice(pDevice);
	S32 i;
	for (i = eaSize(&pState->maingroup) - 1; i >= 0; i--)
	{
		UIWidget *pWidget = pState->maingroup[i];
		if (pWidget)
		{
			ui_WidgetRemoveFromGroup(pWidget);
			ui_WidgetAddToDevice(pWidget, gfxNextDevice(pDevice));
		}
	}
	return true;
}

// Make a new window for editing.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Interface);
void ui_CreateWindow(void)
{
	ui_AuxDeviceCreate(NULL, ui_AuxDeviceMoveWidgets, NULL);
}

RdrDevice *ui_AuxDeviceCreate(const char *pchTitle, GfxAuxDeviceCloseCallback cbClose, UserData pCloseData)
{
	UIAuxDeviceData *pData = NULL;
	if (!pchTitle)
	{
		char achTitle[100];
		sprintf(achTitle, "Screen #%d", s_iDevice++);
	}
	if (cbClose)
	{
		pData = calloc(1, sizeof(*pData));
		pData->cbClose = cbClose;
		pData->pCloseData = pCloseData;
	}
	return gfxAuxDeviceAdd(NULL, pchTitle, AuxDeviceClose, AuxDevicePerFrameCallback, pData);
}
