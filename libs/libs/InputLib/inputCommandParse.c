#include "inputCommandParse.h"
#include "inputLib.h"

// Enable or disable DirectInput keyboard support.
AUTO_CMD_INT(input_state.bEnableDirectInputKeyboard, enableDInputKeyboard) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);

// Enable or disable integrated IME support.
AUTO_CMD_INT(input_state.bEnableIME, enableIME) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);

// Enable or disable Raw Input support.
AUTO_CMD_INT(input_state.bEnableRawInputSupport, enableRawInputSupport) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Enable in-engine translation of keyboard input into character input, versus using the Windows message queue WM_CHAR messages.
AUTO_CMD_INT(input_state.bEnableRawInputManualCharTranslation, enableRawInputManualCharTranslation) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_CATEGORY(Debug);

