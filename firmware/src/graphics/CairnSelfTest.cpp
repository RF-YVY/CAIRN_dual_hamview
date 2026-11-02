#include "configuration.h"
#if defined(CAIRN_MUI) && defined(ARCH_ESP32) && HAS_TFT
#include "../../meshcore/src/IndicatorFiles.h"
#include "CairnClock.h"
#include "CairnBoot.h"
#include "CairnMUI.h"
#include "CairnSelfTest.h"
#include "UptimeClock.h"
#include "generated/ui_320x240/screens.h"
#include "mesh/NodeDB.h"
#include "mesh/Throttle.h"
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <lvgl.h>

namespace
{
constexpr const char *REQUEST = "/cairn-root-qa.request";
const char *stages[] = {"home", "nodes", "chats", "map", "settings", "radio", "network", "files", "hamview", "clock", "modes"};
bool checked = false, running = false, waiting = false;
bool scrollOnly = false;
bool graphOnly = false;
unsigned stage = 0, probes = 0;
uint32_t lastStep = 0, reportOffset = 0, expectedNode = 0;
char prefix[48], reportPath[64];
uint8_t *pixels = nullptr;

bool visible(lv_obj_t *object)
{
    return object && !lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN);
}

bool tap(int x, int y)
{
    lv_obj_update_layout(objects.main_screen);
    lv_point_t point{x, y};
    auto *hit = lv_indev_search_obj(lv_layer_sys(), &point);
    if (!hit)
        hit = lv_indev_search_obj(lv_layer_top(), &point);
    if (!hit)
        hit = lv_indev_search_obj(objects.main_screen, &point);
    if (!hit || lv_obj_has_state(hit, LV_STATE_DISABLED))
        return false;
    lv_obj_send_event(hit, LV_EVENT_CLICKED, nullptr);
    return true;
}

lv_obj_t *findButton(lv_obj_t *parent, const char *caption)
{
    if (!visible(parent))
        return nullptr;
    if (lv_obj_check_type(parent, &lv_label_class) && !strcmp(lv_label_get_text(parent), caption)) {
        auto *button = lv_obj_get_parent(parent);
        if (lv_obj_check_type(button, &lv_button_class))
            return button;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i)
        if (auto *found = findButton(lv_obj_get_child(parent, i), caption))
            return found;
    return nullptr;
}

bool snapshot()
{
    constexpr unsigned size = 480 * 480 * 2;
    if (!pixels)
        pixels = static_cast<uint8_t *>(heap_caps_aligned_alloc(32, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels)
        return false;
    lv_draw_buf_t draw;
    if (lv_draw_buf_init(&draw, 480, 480, LV_COLOR_FORMAT_RGB565, 960, pixels, size) != LV_RESULT_OK ||
        lv_snapshot_take_to_draw_buf(objects.main_screen, LV_COLOR_FORMAT_RGB565, &draw) != LV_RESULT_OK)
        return false;
    char path[96];
    snprintf(path, sizeof(path), "%s-%s.rgb565", prefix, stages[stage]);
    for (unsigned offset = 0; offset < size; offset += 4096)
        if (!indicatorFiles.write(path, offset, pixels + offset, min(4096U, size - offset), offset == 0))
            return false;
    return true;
}

void report(const char *screen, bool valid, bool captured)
{
    char line[320];
    const int length = snprintf(line, sizeof(line),
                                "{\"screen\":\"%s\",\"valid\":%s,\"snapshot\":%s,\"node\":%lu,\"identityMatches\":%s,"
                                "\"clockEpoch\":%lu,\"clockHandoff\":%u,\"resetReason\":%u,\"signalSamples\":%u,"
                                "\"resident\":%s,\"otherFirmware\":%s}\n",
                                screen, valid ? "true" : "false", captured ? "true" : "false",
                                (unsigned long)(nodeDB ? nodeDB->getNodeNum() : 0),
                                nodeDB && nodeDB->getNodeNum() == expectedNode ? "true" : "false", (unsigned long)time(nullptr),
                                CairnClock::handoffStatus(), unsigned(esp_reset_reason()), cairnMuiSignalSamples(),
                                CairnBoot::resident() ? "true" : "false",
                                CairnBoot::residentAvailable(CairnBoot::MeshCore) ? "true" : "false");
    if (length > 0 && length < int(sizeof(line)) &&
        indicatorFiles.write(reportPath, reportOffset, reinterpret_cast<const uint8_t *>(line), length, reportOffset == 0))
        reportOffset += length;
}

void begin()
{
    meshtastic_SdCardInfo card;
    if (!indicatorFiles.info(card) || !card.present || card.busy)
        return;
    uint8_t request[26];
    uint32_t got, size;
    if (!indicatorFiles.read(REQUEST, 0, request, sizeof(request), got, size)) {
        if (++probes >= 3)
            checked = true;
        return;
    }
    checked = true;
    const uint64_t mac = ESP.getEfuseMac();
    if (got != sizeof(request) || size != sizeof(request) ||
        (memcmp(request, "CAIRNQA1", 8) && memcmp(request, "CAIRNSC1", 8) && memcmp(request, "CAIRNSG1", 8)) ||
        memcmp(request + 8, &mac, 6))
        return;
    scrollOnly = !memcmp(request, "CAIRNSC1", 8);
    graphOnly = !memcmp(request, "CAIRNSG1", 8);
    uint64_t nonce;
    memcpy(&nonce, request + 14, sizeof(nonce));
    memcpy(&expectedNode, request + 22, sizeof(expectedNode));
    if (!indicatorFiles.remove(REQUEST))
        return;
    snprintf(prefix, sizeof(prefix), "/cairn-qa-%08lx%08lx", (unsigned long)(nonce >> 32), (unsigned long)nonce);
    snprintf(reportPath, sizeof(reportPath), "%s.jsonl", prefix);
    running = true;
}

lv_obj_t *findScroll(lv_obj_t *parent)
{
    if (!lv_obj_is_visible(parent))
        return nullptr;
    if (lv_obj_has_flag(parent, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_width(parent) == 393 &&
        lv_obj_get_scroll_bottom(parent) > 0)
        return parent;
    for (unsigned i = 0; i < lv_obj_get_child_count(parent); ++i)
        if (auto *found = findScroll(lv_obj_get_child(parent, i)))
            return found;
    return nullptr;
}

bool swipeCards()
{
    tap(32, 82);
    lv_obj_update_layout(objects.main_screen);
    auto *cards = findScroll(objects.main_screen);
    if (!cards)
        return false;
    static lv_point_t point;
    static bool pressed;
    auto *pointer = lv_indev_create();
    if (!pointer)
        return false;
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, lv_display_get_default());
    lv_indev_set_read_cb(pointer, [](lv_indev_t *, lv_indev_data_t *data) {
        data->point = point;
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    });
    point = {250, 410};
    pressed = true;
    lv_indev_read(pointer);
    for (unsigned i = 0; i < 8; ++i) {
        delay(20);
        point.y -= 20;
        lv_indev_read(pointer);
    }
    pressed = false;
    lv_indev_read(pointer);
    const bool moved = lv_obj_get_scroll_y(cards) > 80;
    lv_anim_delete(pointer, nullptr);
    lv_indev_delete(pointer);
    return moved;
}
} // namespace

void cairnSelfTestTick()
{
    if (Time::getUptimeSecs() < 15 || lv_screen_active() != objects.main_screen)
        return;
    if (!checked)
        begin();
    if (!running || !Throttle::hasElapsed(lastStep, 1000))
        return;
    if (graphOnly) {
        running = false;
        tap(32, 82);
        report("signal", cairnMuiSignalSamples() > 0, snapshot());
        return;
    }
    if (scrollOnly) {
        running = false;
        const bool moved = swipeCards();
        report("scroll", moved, snapshot());
        report("switch-start", true, false);
        Serial.println("CAIRN resident switch starts");
        CairnBoot::request(CairnBoot::MeshCore);
        report("switch-failed", false, false);
        return;
    }
    if (!waiting) {
        bool triggered = true;
        switch (stage) {
        case 0:
            triggered = tap(32, 82);
            break;
        case 1:
            triggered = tap(32, 214);
            break;
        case 2:
            triggered = tap(32, 148);
            break;
        case 3:
            triggered = tap(32, 346);
            break;
        case 4:
            triggered = tap(32, 412);
            break;
        case 5:
            triggered = tap(32, 280);
            break;
        case 6:
            triggered = tap(32, 82) && tap(406, 375);
            break;
        case 7:
            triggered = tap(32, 82) && tap(270, 462);
            break;
        case 8:
            triggered = tap(32, 82) && tap(135, 462);
            break;
        case 9:
            triggered = tap(32, 82) && tap(170, 80);
            break;
        case 10:
            triggered = tap(32, 82) && tap(310, 22);
            break;
        }
        if (!triggered)
            report(stages[stage], false, false);
        waiting = true;
        lastStep = Time::getMillis();
        return;
    }
    bool valid = true;
    switch (stage) {
    case 0:
        valid = visible(objects.home_panel);
        break;
    case 1:
        valid = visible(objects.nodes_panel);
        break;
    case 2:
        valid = visible(objects.chats_panel) || visible(objects.messages_panel);
        break;
    case 3:
        valid = visible(objects.map_panel);
        break;
    case 4:
        valid = visible(objects.controller_panel);
        break;
    case 5:
        valid = visible(objects.settings_modem_preset_panel);
        break;
    case 6:
        valid = visible(objects.settings_wifi_panel);
        break;
    }
    report(stages[stage], valid, snapshot());
    if (stage == 5 || stage == 6) {
        if (auto *cancel = findButton(objects.main_screen, "Cancel"))
            lv_obj_send_event(cancel, LV_EVENT_CLICKED, nullptr);
        else
            report("cancel_missing", false, false);
    }
    if (stage == 10) {
        running = false;
        if (auto *button = findButton(objects.main_screen, "Switch to MeshCore")) {
            if (!lv_obj_has_state(button, LV_STATE_DISABLED))
                lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
        }
        report("switch_failed", false, false);
        return;
    }
    ++stage;
    waiting = false;
    lastStep = Time::getMillis();
}
#endif
