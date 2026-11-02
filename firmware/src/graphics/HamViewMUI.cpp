#include "configuration.h"

#if defined(HAMVIEW_MUI) && defined(ARCH_ESP32) && HAS_TFT

#include "UptimeClock.h"
#include "graphics/CairnMUI.h"
#include "graphics/HamViewExtension.h"
#include "lvgl.h"
#include "modules/HamViewModule.h"
#include <stdlib.h>

namespace
{
#if defined(CAIRN_SQUARE_LAYOUT)
constexpr uint32_t COLOR_BG = 0x030106;
constexpr uint32_t COLOR_CARD = 0x10071D;
constexpr uint32_t COLOR_GREEN = 0x00D7E6;
#elif defined(CAIRN_MUI)
constexpr uint32_t COLOR_BG = 0x090C0F;
constexpr uint32_t COLOR_CARD = 0x1C2025;
constexpr uint32_t COLOR_GREEN = 0x23C5B5;
#else
constexpr uint32_t COLOR_BG = 0x171717;
constexpr uint32_t COLOR_CARD = 0x252525;
constexpr uint32_t COLOR_GREEN = 0x67EA94;
#endif
constexpr uint32_t COLOR_ORANGE = 0xF5A742;
constexpr uint32_t COLOR_MUTED = 0xA0A0A0;

#if defined(CAIRN_SQUARE_LAYOUT)
constexpr int PANEL_X = 64;
constexpr int PANEL_Y = 44;
constexpr int PANEL_WIDTH = 416;
constexpr int PANEL_HEIGHT = 436;
constexpr int HEADER_X = 0;
constexpr int HEADER_WIDTH = 480;
constexpr int BANNER_Y = 49;
#else
const int PANEL_X = LV_PCT(12);
const int PANEL_Y = LV_PCT(10);
const int PANEL_WIDTH = LV_PCT(88);
const int PANEL_HEIGHT = LV_PCT(90);
const int HEADER_X = PANEL_X;
const int HEADER_WIDTH = PANEL_WIDTH;
const int BANNER_Y = LV_PCT(11);
#endif

lv_obj_t *launcherButton = nullptr;
lv_obj_t *unreadIndicator = nullptr;
lv_obj_t *panel = nullptr;
lv_obj_t *topPanel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *countLabel = nullptr;
lv_obj_t *spotRows[HamViewModule::MAX_SPOTS] = {};
HamViewSpot visibleSpots[HamViewModule::MAX_SPOTS];
lv_obj_t *settingsPanel = nullptr;
lv_obj_t *usernameField = nullptr;
lv_obj_t *passwordField = nullptr;
lv_obj_t *hostField = nullptr;
lv_obj_t *portField = nullptr;
lv_obj_t *filterField = nullptr;
lv_obj_t *bannerCheckbox = nullptr;
lv_obj_t *ttlDropdown = nullptr;
lv_obj_t *keyboard = nullptr;
lv_obj_t *spotBanner = nullptr;
lv_obj_t *spotBannerLabel = nullptr;
lv_obj_t *muiScreen = nullptr;
lv_timer_t *refreshTimer = nullptr;
uint32_t lastSeenSpotSequence = 0;
uint8_t bannerTicksLeft = 0;

void styleSurface(lv_obj_t *obj, uint32_t color, int radius = 0)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
}

lv_obj_t *makeButton(lv_obj_t *parent, const char *text)
{
    lv_obj_t *button = lv_button_create(parent);
    styleSurface(button, COLOR_CARD, 8);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

void makeFieldLabel(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_MUTED), 0);
}

void dismissBanner()
{
    bannerTicksLeft = 0;
    if (spotBanner)
        lv_obj_add_flag(spotBanner, LV_OBJ_FLAG_HIDDEN);
}

void refresh(lv_timer_t *);

void showPanel()
{
    if (!panel)
        return;
    dismissBanner();
    lv_obj_add_flag(unreadIndicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(topPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(panel);
    lv_obj_move_foreground(topPanel);
    refresh(nullptr);
}

void launcherEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        showPanel();
}

void bannerEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        showPanel();
}

void dismissBannerEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        dismissBanner();
}

void closeEvent(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED)
        hamview_mui_close();
}

void textareaEvent(lv_event_t *event)
{
    if (!keyboard)
        return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(keyboard, lv_event_get_target_obj(event));
        lv_obj_set_height(settingsPanel, LV_PCT(44));
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(keyboard);
    }
}

void keyboardEvent(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_keyboard_set_textarea(keyboard, nullptr);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(settingsPanel, PANEL_HEIGHT);
    }
}

void openSettings(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !settingsPanel)
        return;
    HamViewSettings settings;
    if (hamViewModule)
        hamViewModule->getSettings(settings);
    lv_textarea_set_text(usernameField, settings.username);
    lv_textarea_set_text(passwordField, settings.telnetPassword);
    lv_textarea_set_text(hostField, settings.telnetHost);
    char portText[6];
    snprintf(portText, sizeof(portText), "%u", settings.telnetPort);
    lv_textarea_set_text(portField, portText);
    lv_textarea_set_text(filterField, settings.callsignFilter);
    if (settings.showSpotBanner)
        lv_obj_add_state(bannerCheckbox, LV_STATE_CHECKED);
    else
        lv_obj_remove_state(bannerCheckbox, LV_STATE_CHECKED);
    const uint16_t ttl[] = {5, 15, 30, 60, 120};
    uint32_t selected = 2;
    for (uint32_t i = 0; i < 5; ++i)
        if (ttl[i] == settings.spotTtlMinutes)
            selected = i;
    lv_dropdown_set_selected(ttlDropdown, selected);
    lv_obj_clear_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(settingsPanel);
}

void cancelSettings(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED)
        return;
    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(settingsPanel, PANEL_HEIGHT);
}

void saveSettings(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || !hamViewModule)
        return;
    HamViewSettings settings;
    strlcpy(settings.username, lv_textarea_get_text(usernameField), sizeof(settings.username));
    strlcpy(settings.telnetPassword, lv_textarea_get_text(passwordField), sizeof(settings.telnetPassword));
    strlcpy(settings.telnetHost, lv_textarea_get_text(hostField), sizeof(settings.telnetHost));
    const unsigned long port = strtoul(lv_textarea_get_text(portField), nullptr, 10);
    if (port == 0 || port > UINT16_MAX) {
        lv_textarea_set_text(portField, "7300");
        return;
    }
    settings.telnetPort = static_cast<uint16_t>(port);
    strlcpy(settings.callsignFilter, lv_textarea_get_text(filterField), sizeof(settings.callsignFilter));
    const uint16_t ttl[] = {5, 15, 30, 60, 120};
    settings.spotTtlMinutes = ttl[lv_dropdown_get_selected(ttlDropdown)];
    settings.showSpotBanner = lv_obj_has_state(bannerCheckbox, LV_STATE_CHECKED);
    hamViewModule->saveSettings(settings);
    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void refresh(lv_timer_t *)
{
    if (!panel || !hamViewModule)
        return;
    auto *spots = visibleSpots;
    const HamViewStatus status = hamViewModule->getSnapshot(spots, HamViewModule::MAX_SPOTS);
    if (bannerTicksLeft && --bannerTicksLeft == 0)
        dismissBanner();
    if (status.spotSequence != lastSeenSpotSequence) {
        lastSeenSpotSequence = status.spotSequence;
        if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
            lv_obj_clear_flag(unreadIndicator, LV_OBJ_FLAG_HIDDEN);
        if (status.showSpotBanner && status.spotCount && lv_screen_active() == muiScreen &&
            lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN)) {
            lv_label_set_text_fmt(spotBannerLabel, "New HamAlert: %s\n%s  %s", spots[0].callsign,
                                  spots[0].frequency[0] ? spots[0].frequency : "--", spots[0].mode[0] ? spots[0].mode : "--");
            lv_obj_clear_flag(spotBanner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(spotBanner);
            bannerTicksLeft = 7;
        }
    }
    if (lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN))
        return;
    lv_label_set_text_fmt(statusLabel, "%s  |  %s", status.wifiConnected ? "Wi-Fi" : "Wi-Fi off",
                          status.hamAlertConnected ? "Telnet connected" : status.message);
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(status.hamAlertConnected ? COLOR_GREEN : COLOR_ORANGE), 0);
    lv_label_set_text_fmt(countLabel, "%u active DX spots", status.spotCount);

    for (size_t i = 0; i < HamViewModule::MAX_SPOTS; ++i) {
        if (i < status.spotCount) {
            const uint32_t age = Time::getUptimeSecs() - spots[i].receivedAt;
            lv_label_set_text_fmt(spotRows[i], "%s  %s  %s\n%s  %lus", spots[i].callsign,
                                  spots[i].frequency[0] ? spots[i].frequency : "--", spots[i].mode[0] ? spots[i].mode : "--",
                                  spots[i].comment, static_cast<unsigned long>(age));
            lv_obj_clear_flag(spotRows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(spotRows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void createSettings(lv_obj_t *root)
{
    settingsPanel = lv_obj_create(root);
    lv_obj_set_pos(settingsPanel, PANEL_X, PANEL_Y);
    lv_obj_set_size(settingsPanel, PANEL_WIDTH, PANEL_HEIGHT);
    styleSurface(settingsPanel, COLOR_BG);
    lv_obj_set_style_pad_all(settingsPanel, 8, 0);
    lv_obj_set_style_pad_row(settingsPanel, 5, 0);
    lv_obj_set_flex_flow(settingsPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(settingsPanel, LV_DIR_VER);
    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(settingsPanel);
    lv_label_set_text(title, "HamAlert Telnet");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_GREEN), 0);

    makeFieldLabel(settingsPanel, "HamAlert username");
    usernameField = lv_textarea_create(settingsPanel);
    makeFieldLabel(settingsPanel, "Telnet password (not account password)");
    passwordField = lv_textarea_create(settingsPanel);
    makeFieldLabel(settingsPanel, "Telnet host");
    hostField = lv_textarea_create(settingsPanel);
    makeFieldLabel(settingsPanel, "Telnet port");
    portField = lv_textarea_create(settingsPanel);
    makeFieldLabel(settingsPanel, "Optional callsign filter");
    filterField = lv_textarea_create(settingsPanel);
    for (lv_obj_t *field : {usernameField, passwordField, hostField, portField, filterField}) {
        lv_obj_set_width(field, LV_PCT(100));
#if defined(CAIRN_MUI)
        lv_obj_set_height(field, 44);
        styleSurface(field, COLOR_CARD, 8);
        lv_obj_set_style_text_font(field, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(field, lv_color_white(), 0);
#else
        lv_obj_set_height(field, 30);
#endif
        lv_textarea_set_one_line(field, true);
        lv_obj_add_event_cb(field, textareaEvent, LV_EVENT_ALL, nullptr);
    }
    lv_textarea_set_placeholder_text(usernameField, "HamAlert username");
    lv_textarea_set_placeholder_text(passwordField, "Telnet password");
    lv_textarea_set_password_mode(passwordField, true);
    lv_textarea_set_placeholder_text(hostField, "Telnet host: hamalert.org");
    lv_textarea_set_placeholder_text(portField, "Telnet port: 7300");
    lv_textarea_set_accepted_chars(portField, "0123456789");
    lv_textarea_set_max_length(portField, 5);
    lv_textarea_set_max_length(usernameField, 63);
    lv_textarea_set_max_length(passwordField, 63);
    lv_textarea_set_max_length(hostField, 63);
    lv_textarea_set_max_length(filterField, 23);
    lv_textarea_set_placeholder_text(filterField, "Optional callsign filter");

    bannerCheckbox = lv_checkbox_create(settingsPanel);
    lv_checkbox_set_text(bannerCheckbox, "Show new-spot banner");
    lv_obj_add_state(bannerCheckbox, LV_STATE_CHECKED);

    lv_obj_t *ttlLabel = lv_label_create(settingsPanel);
    lv_label_set_text(ttlLabel, "Keep received spots on screen for:");
    lv_obj_set_style_text_color(ttlLabel, lv_color_hex(COLOR_MUTED), 0);
    ttlDropdown = lv_dropdown_create(settingsPanel);
    lv_obj_set_width(ttlDropdown, LV_PCT(100));
    lv_dropdown_set_options(ttlDropdown, "5 minutes\n15 minutes\n30 minutes\n60 minutes\n120 minutes");

    lv_obj_t *actions = lv_obj_create(settingsPanel);
    lv_obj_set_size(actions, LV_PCT(100), 32);
    styleSurface(actions, COLOR_BG);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 8, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_t *cancel = makeButton(actions, "Cancel");
    lv_obj_t *save = makeButton(actions, "Save");
    lv_obj_set_flex_grow(cancel, 1);
    lv_obj_set_flex_grow(save, 1);
    lv_obj_add_event_cb(cancel, cancelSettings, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(save, saveSettings, LV_EVENT_CLICKED, nullptr);

    keyboard = lv_keyboard_create(root);
    lv_obj_set_size(keyboard, PANEL_WIDTH, LV_PCT(46));
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(keyboard, keyboardEvent, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}
} // namespace

extern "C" void hamview_mui_install(lv_obj_t *mainScreen, lv_obj_t *muiTopPanel, lv_obj_t *)
{
    if (panel || !mainScreen || !muiTopPanel)
        return;
    muiScreen = mainScreen;

    launcherButton = makeButton(muiTopPanel, "HamView");
#if defined(CAIRN_MUI)
    lv_obj_set_size(launcherButton, 88, 32);
#else
    lv_obj_set_size(launcherButton, 68, 22);
#endif
    lv_obj_align(launcherButton, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(launcherButton, lv_color_hex(COLOR_GREEN), 0);
#if defined(CAIRN_MUI)
    lv_obj_set_style_text_color(lv_obj_get_child(launcherButton, 0), lv_color_hex(COLOR_BG), 0);
#endif
    lv_obj_add_event_cb(launcherButton, launcherEvent, LV_EVENT_CLICKED, nullptr);
    unreadIndicator = lv_obj_create(launcherButton);
    lv_obj_set_size(unreadIndicator, 10, 10);
    lv_obj_align(unreadIndicator, LV_ALIGN_TOP_RIGHT, 0, 0);
    styleSurface(unreadIndicator, COLOR_ORANGE, LV_RADIUS_CIRCLE);
    lv_obj_remove_flag(unreadIndicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(unreadIndicator, LV_OBJ_FLAG_HIDDEN);

    topPanel = lv_obj_create(mainScreen);
    lv_obj_set_pos(topPanel, HEADER_X, 0);
    lv_obj_set_size(topPanel, HEADER_WIDTH, PANEL_Y);
    styleSurface(topPanel, COLOR_BG);
    lv_obj_t *back = makeButton(topPanel, "< Mesh");
#if defined(CAIRN_MUI)
    lv_obj_set_size(back, 80, 32);
#else
    lv_obj_set_size(back, 58, 22);
#endif
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(back, closeEvent, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *topTitle = lv_label_create(topPanel);
    lv_label_set_text(topTitle, "HamView");
    lv_obj_set_style_text_color(topTitle, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_align(topTitle, LV_ALIGN_CENTER, 0, 0);

    panel = lv_obj_create(mainScreen);
    lv_obj_set_pos(panel, PANEL_X, PANEL_Y);
    lv_obj_set_size(panel, PANEL_WIDTH, PANEL_HEIGHT);
    styleSurface(panel, COLOR_BG);
    lv_obj_set_style_pad_all(panel, 7, 0);
    lv_obj_set_style_pad_row(panel, 4, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *header = lv_obj_create(panel);
#if defined(CAIRN_MUI)
    lv_obj_set_size(header, LV_PCT(100), 76);
#else
    lv_obj_set_size(header, LV_PCT(100), 32);
#endif
    styleSurface(header, COLOR_CARD, 8);
    lv_obj_set_style_pad_all(header, 5, 0);
    statusLabel = lv_label_create(header);
#if defined(CAIRN_MUI)
    lv_obj_set_width(statusLabel, LV_PCT(76));
    lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
#endif
    lv_label_set_text(statusLabel, "Starting HamView...");
    lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(statusLabel, LV_ALIGN_TOP_LEFT, 0, 0);
    countLabel = lv_label_create(header);
    lv_label_set_text(countLabel, "0 active DX spots");
    lv_obj_set_style_text_color(countLabel, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_text_font(countLabel, &lv_font_montserrat_16, 0);
    lv_obj_align(countLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *gear = makeButton(header, "Setup");
#if defined(CAIRN_MUI)
    lv_obj_set_size(gear, 72, 36);
#else
    lv_obj_set_size(gear, 48, 24);
#endif
    lv_obj_align(gear, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(gear, openSettings, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *list = lv_obj_create(panel);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    styleSurface(list, COLOR_BG);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 3, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    for (lv_obj_t *&row : spotRows) {
        row = lv_label_create(list);
        lv_obj_set_width(row, LV_PCT(100));
        lv_label_set_long_mode(row, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
#if defined(CAIRN_SQUARE_LAYOUT)
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(COLOR_GREEN), 0);
        lv_obj_set_style_pad_all(row, 8, 0);
#endif
        lv_obj_set_style_text_color(row, lv_color_white(), 0);
        lv_obj_set_style_text_font(row, &lv_font_montserrat_16, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    createSettings(mainScreen);
    spotBanner = lv_obj_create(mainScreen);
    lv_obj_set_pos(spotBanner, PANEL_X, BANNER_Y);
    lv_obj_set_size(spotBanner, PANEL_WIDTH, 42);
    styleSurface(spotBanner, COLOR_GREEN, 8);
    lv_obj_set_style_pad_all(spotBanner, 5, 0);
    lv_obj_add_flag(spotBanner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(spotBanner, bannerEvent, LV_EVENT_CLICKED, nullptr);
    spotBannerLabel = lv_label_create(spotBanner);
    lv_obj_set_width(spotBannerLabel, LV_PCT(84));
    lv_label_set_long_mode(spotBannerLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(spotBannerLabel, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_text_font(spotBannerLabel, &lv_font_montserrat_16, 0);
    lv_label_set_text(spotBannerLabel, "New HamAlert spot");
    lv_obj_t *dismiss = makeButton(spotBanner, "X");
    lv_obj_set_size(dismiss, 24, 24);
    lv_obj_align(dismiss, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(dismiss, dismissBannerEvent, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(spotBanner, LV_OBJ_FLAG_HIDDEN);
    if (hamViewModule)
        lastSeenSpotSequence = hamViewModule->getSnapshot(nullptr, 0).spotSequence;
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(topPanel, LV_OBJ_FLAG_HIDDEN);
    refreshTimer = lv_timer_create(refresh, 1000, nullptr);
#if defined(CAIRN_MUI)
    cairnMuiInstall();
#endif
}

extern "C" void hamview_mui_open(void)
{
    showPanel();
}

extern "C" void hamview_mui_close(void)
{
    if (settingsPanel)
        lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
    if (keyboard)
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    if (panel)
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    if (topPanel)
        lv_obj_add_flag(topPanel, LV_OBJ_FLAG_HIDDEN);
}

#endif
