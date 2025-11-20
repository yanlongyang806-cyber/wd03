#pragma once
GCC_SYSTEM

typedef struct UIWindow UIWindow;

typedef struct ValidationWindow ValidationWindow;

typedef enum ValidationStatus {
	VALIDATION_OK,
	VALIDATION_WARNING,
	VALIDATION_ERROR,
} ValidationStatus;



ValidationWindow *validationWindowCreate(void);
void validationWindowDestroy(ValidationWindow *vwind);

UIWindow *validationWindowGetWindow(ValidationWindow *vwind);

// After calling this, call your validation function as normal, and all
//  Errorfs will be redirected to be validation errors.
void validationWindowStartValidating(ValidationWindow *vwind);

// Finalizes all validation done (restores Errorf hooks)
// Shows/hides the window appropriately
// Builds the widgets into the window
void validationWindowStopValidating(ValidationWindow *vwind);

ValidationStatus validationWindowGetStatus(ValidationWindow *vwind);

