#include "CairnDashboard.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static uint16_t framebuffer[480 * 480];
static uint16_t drawBuffer[480 * 40];
static CairnAction lastAction;
static int touchX, touchY;
static bool pressed = false;

static void write16(FILE *file, uint16_t value)
{
    fwrite(&value, 2, 1, file);
}
static void write32(FILE *file, uint32_t value)
{
    fwrite(&value, 4, 1, file);
}

static bool saveBitmap(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file)
        return false;
    fwrite("BM", 2, 1, file);
    write32(file, 54 + 480 * 480 * 3);
    write32(file, 0);
    write32(file, 54);
    write32(file, 40);
    write32(file, 480);
    write32(file, 480);
    write16(file, 1);
    write16(file, 24);
    for (int i = 0; i < 6; ++i)
        write32(file, 0);
    for (int y = 479; y >= 0; --y) {
        for (int x = 0; x < 480; ++x) {
            const uint16_t pixel = framebuffer[y * 480 + x];
            const uint8_t rgb[] = {static_cast<uint8_t>((pixel & 31) * 255 / 31),
                                   static_cast<uint8_t>(((pixel >> 5) & 63) * 255 / 63),
                                   static_cast<uint8_t>(((pixel >> 11) & 31) * 255 / 31)};
            fwrite(rgb, 3, 1, file);
        }
    }
    const bool ok = ferror(file) == 0;
    fclose(file);
    return ok;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2)
        return 2;
    lv_init();
    puts("LVGL initialized");
    auto *display = lv_display_create(480, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, drawBuffer, nullptr, sizeof(drawBuffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
        const int width = lv_area_get_width(area);
        for (int y = area->y1; y <= area->y2; ++y)
            memcpy(framebuffer + y * 480 + area->x1, pixels + (y - area->y1) * width * 2, width * 2);
        lv_display_flush_ready(disp);
    });
    CairnShell shell;
    shell.create(lv_screen_active(), [](CairnAction action) { lastAction = action; });
    puts("Shell created");
    CairnDashboard dashboard;
    dashboard.create(shell.home, [](CairnAction action) { lastAction = action; });
    puts("Dashboard created");
    CairnDashboardState state;
    if (argc > 2 && strcmp(argv[2], "--sample") == 0) {
        strcpy(state.clock, "18:42");
        strcpy(state.date, "Sunday, Nov 1");
        strcpy(state.messageFrom, "Public");
        strcpy(state.messageText, "Camp check-in");
        strcpy(state.network, "Field Wi-Fi");
        strcpy(state.hamStatus, "Connected");
        state.wifiConnected = true;
        state.radioReady = true;
        state.nodes = 79;
        state.messages = 4;
        state.spots = 7;
        state.samples = 120;
        for (unsigned i = 0; i < 120; ++i)
            state.signal[i] = i % 39 < 6 ? -70 - i % 7 : -112 + i % 9;
    }
    dashboard.update(state);
    puts("State applied");
    lv_obj_update_layout(lv_screen_active());
    puts("Layout updated");
    lv_refr_now(display);
    puts("Frame rendered");
    if (!saveBitmap(argv[1]))
        return 3;
    auto *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, [](lv_indev_t *, lv_indev_data_t *data) {
        data->point.x = touchX;
        data->point.y = touchY;
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    });
    auto tap = [pointer](int x, int y) {
        touchX = x;
        touchY = y;
        pressed = true;
        lv_tick_inc(40);
        lv_indev_read(pointer);
        pressed = false;
        lv_tick_inc(40);
        lv_indev_read(pointer);
    };
    const CairnAction expected[] = {CairnAction::Home,  CairnAction::Chats,    CairnAction::Nodes,
                                    CairnAction::Radio, CairnAction::Position, CairnAction::Settings};
    for (unsigned i = 0; i < 6; ++i) {
        tap(32, 82 + i * 66);
        assert(lastAction == expected[i]);
    }
    const CairnAction cards[] = {CairnAction::Tools,    CairnAction::Chats, CairnAction::Nodes,
                                 CairnAction::Position, CairnAction::Radio, CairnAction::Network};
    for (unsigned i = 0; i < 6; ++i) {
        tap(98 + (i % 3) * 134, 206 + (i / 3) * 133);
        assert(lastAction == cards[i]);
    }
    const CairnAction shortcuts[] = {CairnAction::HamView, CairnAction::Files, CairnAction::Tools};
    for (unsigned i = 0; i < 3; ++i) {
        tap(137 + i * 134, 462);
        assert(lastAction == shortcuts[i]);
    }
    puts("Rendered shared LVGL views; sidebar and card-icon touch tests passed");
    tap(170, 80);
    assert(lastAction == CairnAction::Clock);
    lv_obj_t *scrolling = nullptr;
    for (unsigned i = 0; i < lv_obj_get_child_count(shell.home); ++i) {
        auto *child = lv_obj_get_child(shell.home, i);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_bottom(child) > 0)
            scrolling = child;
    }
    assert(scrolling);
    touchX = 250;
    touchY = 410;
    pressed = true;
    lv_tick_inc(40);
    lv_indev_read(pointer);
    for (unsigned i = 0; i < 8; ++i) {
        touchY -= 20;
        lv_tick_inc(20);
        lv_indev_read(pointer);
    }
    pressed = false;
    lv_tick_inc(40);
    lv_indev_read(pointer);
    assert(lv_obj_get_scroll_y(scrolling) > 80);
    assert(lastAction == CairnAction::Clock);
    lv_obj_scroll_to_y(scrolling, 133, LV_ANIM_OFF);
    tap(98, 336);
    assert(lastAction == CairnAction::HamView);
    puts("Dashboard swipe reveals third row without opening a card; revealed HamView card responds.");
    return 0;
}
