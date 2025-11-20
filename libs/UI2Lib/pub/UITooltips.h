/***************************************************************************



***************************************************************************/

#ifndef UI_TOOLTIPS_H
#define UI_TOOLTIPS_H
GCC_SYSTEM

#include "UICore.h"

#define UI_TOOLTIP_DISPLAY_DELAY 0.7f
#define UI_TOOLTIP_FADE_DELAY 0.3f

void ui_TooltipsDisplay(void);
void ui_TooltipsTick(void);
void ui_TooltipsClearActive( void );
void ui_TooltipsSetNoDelay( bool noDelay );

#endif
