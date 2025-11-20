GCC_SYSTEM
#include "ValidationWindow.h"
#include "UIWindow.h"
#include "UISMFView.h"
#include "UIScrollbar.h"

#include "EString.h"
#include "earray.h"
#include "error.h"
#include "utils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct ValidationWindow {
	UIWindow *window;
	char **errors;
	ValidationStatus *severity;
	UISMFView *smfview;
	UIScrollArea *scrollarea;
	F32 savedHeight;
	ValidationStatus lastSeverity;
} ValidationWindow;

ValidationWindow *validationWindowCreate(void)
{
	ValidationWindow *vwind = calloc(sizeof(*vwind), 1);
	vwind->window = ui_WindowCreate("Validation", 0, 0, 1, 1);
	ui_WidgetSetDimensionsEx(UI_WIDGET(vwind->window), 1.f, 100, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(vwind->window), 0, 0, 0, 0, UIBottomLeft);
	ui_WindowSetResizable(vwind->window, true);
	vwind->scrollarea = ui_ScrollAreaCreate(0, 0, 1.f, 1.f, 100, 10000, false, true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(vwind->scrollarea), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(vwind->window, vwind->scrollarea);
	vwind->savedHeight = 0;
	vwind->lastSeverity = VALIDATION_WARNING;
	return vwind;
}

void validationWindowDestroy(ValidationWindow *vwind)
{
	ui_WindowFreeInternal(vwind->window);
	eaDestroyEx(&vwind->errors, NULL);
	eaiDestroy(&vwind->severity);
	free(vwind);
}

UIWindow *validationWindowGetWindow(ValidationWindow *vwind)
{
	return vwind->window;
}

void validationWindowAddError(ValidationWindow *vwind, ValidationStatus severity, const char *errString)
{
	char buf[4096];
	strcpy(buf, errString);
	while(strstriReplace(buf, "\n", "<br>"));
	eaPush(&vwind->errors, strdup(buf));
	eaiPush(&vwind->severity, severity);
}


static ValidationWindow *g_vwind;
static void validationWindowCallback(ErrorMessage *errMsg, void *userdata)
{
	const char *errString = errorFormatErrorMessage(errMsg);
	validationWindowAddError(g_vwind, VALIDATION_ERROR, errString);
}

void validationWindowStartValidating(ValidationWindow *vwind)
{
	assert(!g_vwind);
	g_vwind = vwind;
	eaClearEx(&vwind->errors, NULL);
	eaiClear(&vwind->severity);
	ErrorfPushCallback(validationWindowCallback, NULL);
	pushDontReportErrorsToErrorTracker(true);
}

static void validationWindowReflowCallback(UIWidget *widget_UNUSED, UserData userData)
{
	ValidationWindow *vwind = (ValidationWindow*)userData;
	vwind->scrollarea->ySize = vwind->smfview->widget.height;
}

static void validationWindowUpdateWindow(ValidationWindow *vwind)
{
	char *str=NULL;
	ValidationStatus ret=VALIDATION_OK;
	if (!vwind->smfview) {
		vwind->smfview = ui_SMFViewCreate(0, 0, 100, 100);
		ui_WidgetSetDimensionsEx(UI_WIDGET(vwind->smfview), 1.f, 100, UIUnitPercentage, UIUnitFixed);
		ui_SMFViewSetReflowCallback(vwind->smfview, validationWindowReflowCallback, vwind);
		ui_WidgetAddChild(UI_WIDGET(vwind->scrollarea), UI_WIDGET(vwind->smfview));
	}
	estrStackCreateSize(&str, 16384);
	if (eaiSize(&vwind->severity)==0) {
		estrConcatf(&str, "");
	} else {
		int i;
		for (i=0; i<eaiSize(&vwind->severity); i++) {
			MAX1(ret, vwind->severity[i]);
			switch (vwind->severity[i]) {
				xcase VALIDATION_WARNING:
					estrConcatf(&str, "%s", (i&1)?"<color #AF8F30>":"<color #7F5F00>");
				xcase VALIDATION_ERROR:
					estrConcatf(&str, "%s", (i&1)?"<color #AF3030>":"<color #7F0000>");
			}
			estrConcatf(&str, "%s", vwind->errors[i]);
			estrConcatf(&str, "</color>");
			if (!strEndsWith(vwind->errors[i], "<br>"))
				estrConcatf(&str, "<br>");
		}
	}
	ui_SMFViewSetText(vwind->smfview, str, NULL);
	estrDestroy(&str);
	switch (ret) {
	xcase VALIDATION_OK:
		ui_WindowSetTitle(vwind->window, "Validation: PASSED");
	xcase VALIDATION_WARNING:
		ui_WindowSetTitle(vwind->window, "Validation: Warnings");
	xcase VALIDATION_ERROR:
		ui_WindowSetTitle(vwind->window, "Validation: FAILED");
	}
	if ((ret == VALIDATION_OK) != (vwind->lastSeverity == VALIDATION_OK)) {
		F32 heightToSave = vwind->window->widget.height;
		vwind->lastSeverity = ret;
		ui_WidgetSetDimensionsEx(UI_WIDGET(vwind->window), 1.f, vwind->savedHeight, UIUnitPercentage, UIUnitFixed);
		vwind->savedHeight = heightToSave;
	}
}


void validationWindowStopValidating(ValidationWindow *vwind)
{
	popDontReportErrorsToErrorTracker();
	ErrorfPopCallback();
	assert(g_vwind);
	g_vwind = NULL;

	validationWindowUpdateWindow(vwind);
}

ValidationStatus validationWindowGetStatus(ValidationWindow *vwind)
{
	ValidationStatus ret=VALIDATION_OK;
	int i;
	for (i=0; i<eaiSize(&vwind->severity); i++) {
		MAX1(ret, vwind->severity[i]);
	}
	return ret;
}

