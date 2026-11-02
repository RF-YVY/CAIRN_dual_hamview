#include "configuration.h"
#if defined(CAIRN_MUI) && defined(ARCH_ESP32) && HAS_TFT
#include <lvgl.h>
extern "C" {
LV_FONT_DECLARE(cairn_clock_72);
#include "../../meshcore/src/fonts/cairn_clock_72.c"
}
#endif
