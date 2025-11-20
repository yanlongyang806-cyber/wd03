#include "UGCProductViewer.h"

#include "UGCEditorMain.h"
#include "UGCProjectChooser.h"
#include "UICore.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void UGCProductViewer_Enter(void)
{
	if (ugcProjectChooser_IsOpen())
	{
		ugcProjectChooserHide();
	}
	ugcEditorToggleSkin(true);
	ui_ShowGameUI( false );

	UGCProductViewer_Refresh();
}

void UGCProductViewer_Leave(void)
{
	UGCProductViewer_Destroy();

	if (ugcProjectChooser_IsOpen())
	{
		ugcProjectChooserShow();
	}
	else
	{
		ugcEditorToggleSkin(false);
		ui_ShowGameUI( true );
	}
}

void UGCProductViewer_BeginFrame(void)
{
	UGCProductViewer_OncePerFrame();
}
