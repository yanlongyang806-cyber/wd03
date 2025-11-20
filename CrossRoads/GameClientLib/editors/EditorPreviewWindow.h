#pragma once

// Show the preview window for the specified resources
void PreviewResource(const char *pDictName, const char *pResourceName);

// Display/hide the Preview window
void ShowPreviewWindow(bool bShow);

// Returns if the Preview window is visible
bool CheckPreviewWindow(void);

// Returns reference to preview window state
bool *GetPreviewWindowStatus(void);