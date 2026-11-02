#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Firmware extension points for the HamView MUI variant.
void hamview_mui_install(lv_obj_t *main_screen, lv_obj_t *top_panel, lv_obj_t *home_panel);
void hamview_mui_close(void);

#ifdef __cplusplus
}
#endif
