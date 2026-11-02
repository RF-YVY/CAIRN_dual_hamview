#include "configuration.h"
#if defined(CAIRN_MUI) && defined(ARCH_ESP32) && HAS_TFT
#include "../../meshcore/src/CairnDashboard.h"
#include "../../meshcore/src/IndicatorFiles.h"
#include "UptimeClock.h"
#include "generated/ui_320x240/screens.h"
#include "graphics/CairnBoot.h"
#include "graphics/CairnClock.h"
#include "graphics/CairnMUI.h"
#include "graphics/CairnSelfTest.h"
#include "graphics/HamViewExtension.h"
#include "graphics/common/LoRaPresets.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "mesh/Throttle.h"
#include "modules/HamViewModule.h"
#include <WiFi.h>
#include <time.h>

namespace
{
CairnShell shell;
CairnDashboard dashboard;
CairnDashboardState state;
uint32_t lastSignalSample = 0;
bool installed = false;
bool (*navigationGuard)(void *) = nullptr;
void *navigationContext = nullptr;
lv_obj_t *protocolPanel = nullptr;
String storagePath = "/";
uint32_t storageOffset = 0;
meshtastic_DirectoryListing storageListing;
uint8_t filePreview[4097];
lv_obj_t *clockDisplayChoice = nullptr, *clockZoneChoice = nullptr;

lv_obj_t *overlay(const char *heading)
{
    if (protocolPanel)
        lv_obj_delete(protocolPanel);
    protocolPanel = lv_obj_create(objects.main_screen);
    lv_obj_set_pos(protocolPanel, 64, 44);
    lv_obj_set_size(protocolPanel, 416, 436);
    lv_obj_set_style_bg_color(protocolPanel, lv_color_hex(0x030106), 0);
    lv_obj_set_style_border_width(protocolPanel, 0, 0);
    lv_obj_set_style_pad_all(protocolPanel, 16, 0);
    lv_obj_set_style_text_color(protocolPanel, lv_color_hex(0xF4F0FF), 0);
    lv_obj_set_flex_flow(protocolPanel, LV_FLEX_FLOW_COLUMN);
    auto *title = lv_label_create(protocolPanel);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_label_set_text(title, heading);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00D7E6), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    return protocolPanel;
}

lv_obj_t *overlayLabel(const char *text)
{
    auto *label = lv_label_create(protocolPanel);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF4F0FF), 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *overlayButton(const char *text, lv_event_cb_t callback, void *context = nullptr)
{
    auto *button = lv_button_create(protocolPanel);
    lv_obj_set_size(button, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x10071D), 0);
    lv_obj_set_style_text_color(button, lv_color_hex(0xF4F0FF), 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    auto *caption = lv_label_create(button);
    lv_obj_set_width(caption, LV_PCT(95));
    lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_16, 0);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, context);
    return button;
}

void openFiles();

void styleDropdown(lv_obj_t *object)
{
    for (auto *part : {object, lv_dropdown_get_list(object)}) {
        lv_obj_set_style_bg_color(part, lv_color_hex(0x10071D), 0);
        lv_obj_set_style_text_color(part, lv_color_hex(0xF4F0FF), 0);
        lv_obj_set_style_text_font(part, &lv_font_montserrat_16, 0);
        lv_obj_set_style_border_color(part, lv_color_hex(0x30203D), 0);
        lv_obj_set_style_shadow_width(part, 0, 0);
    }
}

void openClock(lv_event_t *)
{
    overlay("Clock display");
    overlayLabel("Display time as");
    clockDisplayChoice = lv_dropdown_create(protocolPanel);
    lv_obj_set_size(clockDisplayChoice, LV_PCT(100), 44);
    lv_dropdown_set_options(clockDisplayChoice, "UTC\nLocal time");
    lv_dropdown_set_selected(clockDisplayChoice, CairnClock::local() ? 1 : 0);
    styleDropdown(clockDisplayChoice);
    overlayLabel("Local time zone");
    clockZoneChoice = lv_dropdown_create(protocolPanel);
    lv_obj_set_size(clockZoneChoice, LV_PCT(100), 44);
    lv_dropdown_set_options(clockZoneChoice, CairnClock::zoneOptions());
    lv_dropdown_set_selected(clockZoneChoice, CairnClock::zone());
    styleDropdown(clockZoneChoice);
    overlayLabel("Daylight saving time is automatic for zones that use it.");
    auto changed = [](lv_event_t *) {
        CairnClock::save(lv_dropdown_get_selected(clockDisplayChoice) != 0, lv_dropdown_get_selected(clockZoneChoice));
    };
    lv_obj_add_event_cb(clockDisplayChoice, changed, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(clockZoneChoice, changed, LV_EVENT_VALUE_CHANGED, nullptr);
}

void openFiles()
{
    overlay("Files");
    meshtastic_SdCardInfo info;
    if (!indicatorFiles.info(info) || !info.present) {
        overlayLabel("SD card unavailable.");
        return;
    }
    char status[96];
    snprintf(status, sizeof(status), "%s  %.1f GiB / %.1f GiB free", info.fat_type == 2 ? "FAT32" : "SD card",
             info.card_size / 1073741824.0, info.free_bytes / 1073741824.0);
    overlayLabel(status);
    overlayLabel(storagePath.c_str());
    if (storagePath != "/")
        overlayButton("Parent folder", [](lv_event_t *) {
            storagePath.remove(storagePath.length() - 1);
            storagePath = storagePath.substring(0, storagePath.lastIndexOf('/') + 1);
            storageOffset = 0;
            openFiles();
        });
    if (!indicatorFiles.list(storagePath.c_str(), storageOffset, storageListing)) {
        overlayLabel("Cannot read this folder.");
        return;
    }
    for (unsigned i = 0; i < storageListing.filenames_count; ++i) {
        auto *path = new String(storagePath + storageListing.filenames[i]);
        if (path->length() > 255) {
            delete path;
            continue;
        }
        auto *entry = overlayButton(
            storageListing.filenames[i],
            [](lv_event_t *event) {
                const String selected = *static_cast<String *>(lv_event_get_user_data(event));
                if (selected.endsWith("/")) {
                    storagePath = selected;
                    storageOffset = 0;
                    openFiles();
                    return;
                }
                overlay("File");
                overlayLabel(selected.c_str());
                uint32_t got, size;
                if (indicatorFiles.read(selected.c_str(), 0, filePreview, 4096, got, size)) {
                    char length[40];
                    snprintf(length, sizeof(length), "%lu bytes", (unsigned long)size);
                    overlayLabel(length);
                    String extension = selected;
                    extension.toLowerCase();
                    if (extension.endsWith(".txt") || extension.endsWith(".json") || extension.endsWith(".csv") ||
                        extension.endsWith(".log")) {
                        for (unsigned j = 0; j < got; ++j)
                            if (filePreview[j] < 0x20 && filePreview[j] != '\n' && filePreview[j] != '\t')
                                filePreview[j] = ' ';
                        filePreview[got] = 0;
                        overlayLabel(reinterpret_cast<const char *>(filePreview));
                    }
                } else
                    overlayLabel("Cannot open this file.");
                overlayButton("Back to files", [](lv_event_t *) { openFiles(); });
            },
            path);
        lv_obj_add_event_cb(
            entry, [](lv_event_t *event) { delete static_cast<String *>(lv_event_get_user_data(event)); }, LV_EVENT_DELETE, path);
    }
    if (!storageListing.total_count)
        overlayLabel("This folder is empty.");
    if (storageOffset)
        overlayButton("Previous", [](lv_event_t *) {
            storageOffset = storageOffset > 16 ? storageOffset - 16 : 0;
            openFiles();
        });
    if (storageOffset + storageListing.filenames_count < storageListing.total_count)
        overlayButton("Next", [](lv_event_t *) {
            storageOffset += storageListing.filenames_count;
            openFiles();
        });
}

void openProtocols(lv_event_t *)
{
    overlay("Radio protocol");
    auto *status = overlayLabel("");
    CairnBoot::Bank bank;
    meshtastic_SdCardInfo card;
    const bool direct = CairnBoot::resident();
    const bool available = direct ? CairnBoot::residentAvailable(CairnBoot::MeshCore)
                                  : indicatorFiles.info(card) && card.present && !card.busy && !card.unformatted &&
                                        CairnBoot::loaderAvailable() && CairnBoot::loadBank(CairnBoot::MeshCore, bank);
    lv_label_set_text(status, !available ? "Protocol switching is not provisioned."
                              : direct   ? "Current: Meshtastic\nBoth firmwares and their separate data stay in internal flash."
                                         : "Current: Meshtastic\nProfiles retain their own identity and data on the SD card.");
    auto *button = overlayButton(
        "Switch to MeshCore",
        [](lv_event_t *event) {
            if (!CairnBoot::request(CairnBoot::MeshCore))
                lv_label_set_text(static_cast<lv_obj_t *>(lv_event_get_user_data(event)),
                                  "Switch unavailable. Check the installed profiles.");
        },
        status);
    lv_obj_set_size(button, LV_PCT(100), 52);
    if (!available)
        lv_obj_add_state(button, LV_STATE_DISABLED);
    overlayLabel(direct ? "Switching restarts the device. No SD backup or restore is needed."
                        : "Switching takes several minutes. Keep the SD card inserted.");
    overlayButton("Clock display settings", openClock);
}

void navigate(CairnAction action)
{
    if (navigationGuard && navigationGuard(navigationContext))
        return;
    if (protocolPanel) {
        lv_obj_delete(protocolPanel);
        protocolPanel = nullptr;
    }
    hamview_mui_close();
    if (action == CairnAction::Clock) {
        openClock(nullptr);
        return;
    }
    if (action == CairnAction::Files) {
        openFiles();
        return;
    }
    if (action == CairnAction::HamView) {
        hamview_mui_open();
        return;
    }
    lv_obj_t *target = nullptr;
    switch (action) {
    case CairnAction::Home:
        target = objects.home_button;
        break;
    case CairnAction::Chats:
        target = objects.messages_button;
        break;
    case CairnAction::Nodes:
        target = objects.nodes_button;
        break;
    case CairnAction::Radio:
        lv_obj_send_event(objects.settings_button, LV_EVENT_CLICKED, nullptr);
        target = objects.basic_settings_modem_preset_button;
        break;
    case CairnAction::Position:
        target = objects.map_button;
        break;
    case CairnAction::Network:
        lv_obj_send_event(objects.settings_button, LV_EVENT_CLICKED, nullptr);
        target = objects.basic_settings_wifi_button;
        break;
    case CairnAction::Files:
        target = objects.home_sd_card_button;
        break;
    default:
        target = objects.settings_button;
        break;
    }
    if (action == CairnAction::Home) {
        lv_obj_remove_flag(shell.home, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(shell.header, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(shell.home, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(shell.header, LV_OBJ_FLAG_HIDDEN);
    }
    shell.select(action);
    if (target)
        lv_obj_send_event(target, LV_EVENT_CLICKED, nullptr);
}

void refresh(lv_timer_t *)
{
    cairnSelfTestTick();
    if (lv_obj_has_flag(objects.home_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(shell.home, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(shell.header, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(shell.home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(shell.header, LV_OBJ_FLAG_HIDDEN);
    const time_t now = time(nullptr);
    tm local{};
    if (now > 1577836800 && localtime_r(&now, &local)) {
        CairnClock::format(now, config.display.use_12h_clock, state.clock, sizeof(state.clock), state.date, sizeof(state.date),
                           state.clockZone, sizeof(state.clockZone));
    }
    state.nodes = nodeDB ? nodeDB->getNumMeshNodes() : 0;
    strlcpy(state.messageFrom, "Messages", sizeof(state.messageFrom));
    strlcpy(state.messageText, lv_label_get_text(objects.home_mail_label), sizeof(state.messageText));
    state.radioReady = config.lora.region != meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    if (state.radioReady && router && router->getRadioIface() && Throttle::hasElapsed(lastSignalSample, 500)) {
        lastSignalSample = Time::getMillis();
        const int16_t rssi = router->getRadioIface()->getSignalStrength();
        if (rssi >= -140 && rssi < 0) {
            if (state.samples == 120) {
                memmove(state.signal, state.signal + 1, 119 * sizeof(int16_t));
                --state.samples;
            }
            state.signal[state.samples++] = rssi;
        }
    }
    state.frequency = !state.radioReady ? 0
                      : config.lora.use_preset
                          ? LoRaPresets::getRadioFreq(config.lora.region, config.lora.modem_preset, config.lora.channel_num)
                          : config.lora.override_frequency;
    state.frequency += config.lora.frequency_offset;
    state.bandwidth = config.lora.bandwidth;
    state.sf = config.lora.spread_factor;
    strlcpy(state.radioProfile,
            config.lora.use_preset ? LoRaPresets::modemPresetToString(config.lora.modem_preset) : "Custom modem",
            sizeof(state.radioProfile));
    state.wifiConnected = WiFi.status() == WL_CONNECTED;
    strlcpy(state.network, state.wifiConnected ? WiFi.SSID().c_str() : "Wi-Fi off", sizeof(state.network));
    meshtastic_PositionLite position{};
    if (nodeDB && nodeDB->copyNodePosition(nodeDB->getNodeNum(), position) && (position.latitude_i || position.longitude_i)) {
        strlcpy(state.position, "Saved", sizeof(state.position));
        snprintf(state.positionDetail, sizeof(state.positionDetail), "%.1f, %.1f", position.latitude_i / 1e7,
                 position.longitude_i / 1e7);
    }
    if (hamViewModule) {
        const auto status = hamViewModule->getSnapshot(nullptr, 0);
        state.spots = status.spotCount;
        strlcpy(state.hamStatus, status.hamAlertConnected ? "Connected" : status.message, sizeof(state.hamStatus));
    }
    dashboard.update(state);
}
} // namespace

unsigned cairnMuiSignalSamples()
{
    return state.samples;
}

void cairnMuiInstall()
{
    if (installed || !objects.home_panel)
        return;
    installed = true;
    lv_textarea_set_password_mode(objects.settings_wifi_password_textarea, true);
    for (uint32_t i = 0; i < lv_obj_get_child_count(objects.main_screen); ++i) {
        auto *child = lv_obj_get_child(objects.main_screen, i);
        if (lv_obj_get_style_x(child, 0) == LV_PCT(12))
            lv_obj_set_x(child, 64);
        if (lv_obj_get_style_y(child, 0) == LV_PCT(10))
            lv_obj_set_y(child, 44);
        if (lv_obj_get_style_width(child, 0) == LV_PCT(88))
            lv_obj_set_width(child, 416);
        if (lv_obj_get_style_height(child, 0) == LV_PCT(90))
            lv_obj_set_height(child, 436);
        if (lv_obj_get_style_height(child, 0) == LV_PCT(10))
            lv_obj_set_height(child, 44);
    }
    lv_obj_add_flag(objects.home_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.button_panel, LV_OBJ_FLAG_HIDDEN);
    shell.create(objects.main_screen, navigate);
    dashboard.create(shell.home, navigate);
    auto *modes = lv_button_create(shell.header);
    lv_obj_set_pos(modes, 274, 10);
    lv_obj_set_size(modes, 76, 25);
    lv_obj_set_style_bg_color(modes, lv_color_hex(0x10071D), 0);
    auto *caption = lv_label_create(modes);
    lv_label_set_text(caption, "Modes");
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    lv_obj_center(caption);
    lv_obj_add_event_cb(modes, openProtocols, LV_EVENT_CLICKED, nullptr);
    strlcpy(state.protocol, "MESHTASTIC", sizeof(state.protocol));
    lv_timer_create(refresh, 250, nullptr);
    refresh(nullptr);
}

bool cairnMuiReady()
{
    return installed;
}

void cairnMuiSetNavigationGuard(bool (*guard)(void *), void *context)
{
    navigationGuard = guard;
    navigationContext = context;
}
#endif
