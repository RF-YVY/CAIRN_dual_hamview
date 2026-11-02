#include "CairnCoreUI.h"
#include "CairnArchive.h"
#include "CairnCompanion.h"
#include "CairnDashboard.h"
#include "CairnJournal.h"
#include "CairnLocalInterface.h"
#include "CairnMap.h"
#include "CairnRadioTools.h"
#include "IndicatorFiles.h"
#include "MyMesh.h"
#include "UptimeClock.h"
#include "concurrency/OSThread.h"
#include "graphics/CairnBoot.h"
#include "graphics/CairnClock.h"
#include "graphics/CairnMUI.h"
#include "graphics/HamViewExtension.h"
#include "graphics/LGFX/LGFX_INDICATOR.h"
#include "mesh/Throttle.h"
#include "modules/HamViewModule.h"
#include "target.h"
#include <Preferences.h>
#include <SPIFFS.h>
#include <Utils.h>
#include <WiFi.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include <lvgl.h>

namespace
{
constexpr uint32_t BG = 0x030106;
constexpr uint32_t CARD = 0x10071D;
constexpr uint32_t TEAL = 0x00D7E6;
constexpr uint32_t MUTED = 0xB6A9D2;
constexpr uint32_t AMBER = 0xFFE500;
constexpr float BANDWIDTHS[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125, 250, 500};
enum class Page {
    Home,
    Chats,
    Nodes,
    Radio,
    Network,
    Storage,
    Position,
    Settings,
    Hardware,
    Appearance,
    System,
    Files,
    File,
    Conversation,
    History,
    Node,
    ChannelSetup,
    Protocol,
    Bluetooth,
    WifiCompanion,
    FileEdit,
    FileDelete,
    FileNew,
    Spectrum,
    Tools,
    ExportHistory,
    Map,
    MapSettings,
    Count
};

LGFX_INDICATOR display;
MyMesh *coreMesh = nullptr;
DataStore *storage = nullptr;
bool initialized = false;
bool meshReady = false;
const char *meshStatus = "Starting MeshCore...";
Page activePage = Page::Home;
lv_obj_t *root;
lv_obj_t *header;
lv_obj_t *home;
lv_obj_t *body;
lv_obj_t *keyboard;
lv_indev_t *pointerInput = nullptr;
uint16_t *screenCapture = nullptr;
uint8_t *captureSnapshot = nullptr;
bool touchOverride = false;
bool touchPressed = false;
uint16_t touchX = 0, touchY = 0;
uint32_t touchReads = 0, touchEvents = 0;
CairnShell shell;
CairnDashboard dashboard;
CairnDashboardState dashboardState;
uint32_t lastSignalSample = 0;
void dashboardAction(CairnAction action);
void navigation(lv_event_t *event);
void showPage(Page page);
String directoryPath = "/";
String selectedFile;
uint32_t directoryOffset = 0;
meshtastic_DirectoryListing fileListing;
static uint8_t fileText[4097];
bool use12hClock = false;
uint8_t brightness = 180;
lv_obj_t *viewStatus = nullptr;
lv_obj_t *runtimeStatus = nullptr;
lv_obj_t *viewText = nullptr;
lv_obj_t *freqField;
lv_obj_t *bwDropdown;
lv_obj_t *sfDropdown;
lv_obj_t *crDropdown;
lv_obj_t *scopeField;
lv_obj_t *ssidField;
lv_obj_t *passwordField;
lv_obj_t *latitudeField;
lv_obj_t *longitudeField;
ContactInfo selectedContact;
uint8_t selectedChannel = 0;
bool directChat = false;
lv_obj_t *conversationLog = nullptr, *composeField = nullptr;
lv_obj_t *fileEditor = nullptr, *newFileName = nullptr, *ownerField = nullptr;
bool restartPending = false;
uint32_t restartRequested = 0;
lv_obj_t *scanChart = nullptr, *waterfall = nullptr;
lv_chart_series_t *scanSeries = nullptr, *peakSeries = nullptr;
uint16_t *waterfallPixels = nullptr;
lv_obj_t *mapImages[CairnMap::TILES]{}, *mapLoading[CairnMap::TILES]{};
lv_image_dsc_t mapDescriptors[CairnMap::TILES]{};
lv_obj_t *mapURL = nullptr, *mapCredit = nullptr;
double mapLatitude = 37.5, mapLongitude = -96.5;
unsigned mapZoom = 5;
bool mapPositioned = false;
lv_obj_t *loginField = nullptr, *channelNameField = nullptr, *channelKeyField = nullptr, *channelSlot = nullptr;
char nodeStatus[160] = {};
uint32_t renderedRevision = 0;
struct NetworkSettings {
    char ssid[33] = {};
    char password[64] = {};
};

NetworkSettings loadNetwork()
{
    NetworkSettings settings;
    Preferences prefs;
    if (prefs.begin("cairn_net", true)) {
        if (prefs.getBytesLength("wifi") == sizeof(settings))
            prefs.getBytes("wifi", &settings, sizeof(settings));
        prefs.end();
    }
    settings.ssid[32] = settings.password[63] = 0;
    return settings;
}

bool protectedFile(const String &path)
{
    return path.startsWith("/cairn-mt-") || path.startsWith("/cairn-mc-") || path == "/cairn-root-qa.request" ||
           (CairnArchive::active() && path == CairnArchive::path());
}

void requestRestart()
{
    restartPending = true;
    restartRequested = Time::getMillis();
}

void surface(lv_obj_t *obj, uint32_t color, int radius = 0)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_text_color(obj, lv_color_white(), 0);
}

lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color = MUTED)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, void *data = nullptr)
{
    lv_obj_t *obj = lv_button_create(parent);
    surface(obj, CARD, 10);
    lv_obj_set_height(obj, 44);
    lv_obj_t *caption = lv_label_create(obj);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, data);
    return obj;
}

void closeKeyboard()
{
    lv_keyboard_set_textarea(keyboard, nullptr);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(body, 436);
}

void fieldEvent(lv_event_t *event)
{
    lv_keyboard_set_textarea(keyboard, lv_event_get_target_obj(event));
    lv_obj_set_height(body, LV_PCT(44));
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard);
    lv_obj_scroll_to_view(lv_event_get_target_obj(event), LV_ANIM_OFF);
}

lv_obj_t *field(lv_obj_t *parent, const char *caption, const char *text, size_t maxLength)
{
    label(parent, caption);
    lv_obj_t *obj = lv_textarea_create(parent);
    lv_obj_set_size(obj, LV_PCT(100), 44);
    surface(obj, CARD, 8);
    lv_textarea_set_one_line(obj, true);
    lv_textarea_set_max_length(obj, maxLength);
    lv_textarea_set_text(obj, text);
    lv_obj_add_event_cb(obj, fieldEvent, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(obj, fieldEvent, LV_EVENT_CLICKED, nullptr);
    return obj;
}

lv_obj_t *dropdown(lv_obj_t *parent, const char *caption, const char *options, uint32_t selected)
{
    label(parent, caption);
    lv_obj_t *obj = lv_dropdown_create(parent);
    lv_obj_set_size(obj, LV_PCT(100), 44);
    lv_dropdown_set_options(obj, options);
    lv_dropdown_set_selected(obj, selected);
    return obj;
}

String contactsText()
{
    if (!meshReady)
        return meshStatus;
    String text;
    auto iterator = coreMesh->startContactsIterator();
    ContactInfo contact;
    while (iterator.hasNext(coreMesh, contact)) {
        char prefix[9];
        mesh::Utils::toHex(prefix, contact.id.pub_key, 4);
        text += contact.name;
        text += "\n";
        text += prefix;
        text += contact.out_path_len == OUT_PATH_UNKNOWN ? "  Route unknown\n\n" : "  Route available\n\n";
    }
    return text.length() ? text : "No nodes discovered yet.\nListening for MeshCore advertisements.";
}

void renderConversation()
{
    if (!conversationLog)
        return;
    lv_obj_clean(conversationLog);
    unsigned shown = 0;
    for (size_t i = messageJournal.count(); i > 0; --i) {
        const auto *message = messageJournal.at(i - 1);
        if (activePage != Page::History &&
            (directChat ? memcmp(message->peer, selectedContact.id.pub_key, 32) != 0 || message->channel != 255
                        : message->channel != selectedChannel))
            continue;
        auto *bubble = lv_obj_create(conversationLog);
        surface(bubble, message->direction ? 0x07232B : CARD, 10);
        lv_obj_set_width(bubble, LV_PCT(96));
        lv_obj_set_height(bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(bubble, 10, 0);
        lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
        auto *name = label(bubble, message->direction ? "You" : message->name, TEAL);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        label(bubble, message->text, 0xF4F0FF);
        const char *statuses[] = {"Received", "Queued", "Sent", "Delivered", "Failed"};
        char detail[48];
        const time_t timestamp = message->timestamp;
        tm date{};
        gmtime_r(&timestamp, &date);
        snprintf(detail, sizeof(detail), "%02d:%02d UTC  %s", date.tm_hour, date.tm_min,
                 message->status <= CairnJournal::Failed ? statuses[message->status] : "Unknown");
        auto *status = label(bubble, detail);
        lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
        ++shown;
    }
    if (!shown)
        label(conversationLog, "No messages yet.");
    lv_obj_scroll_to_y(conversationLog, LV_COORD_MAX, LV_ANIM_OFF);
    renderedRevision = messageJournal.revision();
}

void sentResponse(const uint8_t *response, size_t length, void *context)
{
    const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context));
    if (length && response[0] == 0)
        messageJournal.update(id, CairnJournal::Sent);
    else if (length >= 10 && response[0] == 6) {
        uint32_t ack;
        memcpy(&ack, response + 2, sizeof(ack));
        messageJournal.update(id, CairnJournal::Sent, ack);
    } else
        messageJournal.update(id, CairnJournal::Failed);
}

void nodeResponse(const uint8_t *response, size_t length, void *)
{
    strlcpy(nodeStatus,
            length && (response[0] == 0 || response[0] == 6) ? "Request sent. Waiting for response." : "Request failed.",
            sizeof(nodeStatus));
}

void discoverNodes(lv_event_t *)
{
    uint8_t command[7] = {55, 0x80, 0x04};
    const uint32_t tag = esp_random();
    memcpy(command + 3, &tag, 4);
    if (!localInterface.submit(command, sizeof(command), nodeResponse, nullptr))
        strlcpy(nodeStatus, "Radio command busy.", sizeof(nodeStatus));
}

void observeMeshFrame(const uint8_t *frame, size_t length)
{
    if (length >= 8 && (frame[0] == 0x85 || frame[0] == 0x86) && !memcmp(frame + 2, selectedContact.id.pub_key, 6))
        strlcpy(nodeStatus,
                frame[0] == 0x86 ? "Login rejected."
                : frame[1]       ? "Logged in with admin permissions."
                                 : "Logged in with guest permissions.",
                sizeof(nodeStatus));
    else if (length >= 2 && frame[0] == 0x8E)
        strlcpy(nodeStatus, "Nearby repeater answered discovery.", sizeof(nodeStatus));
    else if (length >= 8 && frame[0] == 0x87 && !memcmp(frame + 2, selectedContact.id.pub_key, 6))
        snprintf(nodeStatus, sizeof(nodeStatus), "Status response received: %u bytes.", unsigned(length - 8));
}

void sendChat(lv_event_t *)
{
    const char *text = lv_textarea_get_text(composeField);
    const size_t length = strlen(text);
    if (!meshReady || !length || length > 148) {
        lv_label_set_text(viewStatus, "Enter a message of 1 to 148 UTF-8 bytes.");
        return;
    }
    const uint32_t timestamp = rtc_clock.getCurrentTimeUnique();
    uint8_t command[MAX_FRAME_SIZE] = {};
    command[0] = directChat ? 2 : 3;
    command[2] = directChat ? 0 : selectedChannel;
    memcpy(command + 3, &timestamp, sizeof(timestamp));
    const unsigned offset = directChat ? 13 : 7;
    if (directChat)
        memcpy(command + 7, selectedContact.id.pub_key, 6);
    memcpy(command + offset, text, length);
    ChannelDetails channel{};
    if (!directChat && !coreMesh->getChannel(selectedChannel, channel)) {
        lv_label_set_text(viewStatus, "Channel unavailable.");
        return;
    }
    const auto id =
        messageJournal.add(directChat ? selectedContact.name : channel.name, text, timestamp,
                           directChat ? selectedContact.id.pub_key : nullptr, directChat ? 255 : selectedChannel, true);
    if (!localInterface.submit(command, offset + length, sentResponse, reinterpret_cast<void *>(uintptr_t(id)))) {
        messageJournal.update(id, CairnJournal::Failed);
        lv_label_set_text(viewStatus, "Radio command busy. Try again.");
        return;
    }
    lv_textarea_set_text(composeField, "");
    closeKeyboard();
    lv_label_set_text(viewStatus, messageJournal.healthy() ? "" : "History storage unavailable.");
    renderConversation();
}

void refresh(lv_timer_t *)
{
    tm now{};
    if (CairnClock::timeSynchronized() && getLocalTime(&now, 0) && now.tm_year >= 124) {
        CairnClock::format(time(nullptr), use12hClock, dashboardState.clock, sizeof(dashboardState.clock), dashboardState.date,
                           sizeof(dashboardState.date), dashboardState.clockZone, sizeof(dashboardState.clockZone));
    }
    dashboardState.radioReady = meshReady;
    dashboardState.nodes = meshReady ? coreMesh->getNumContacts() : 0;
    dashboardState.messages = messageJournal.count();
    if (const auto *message = messageJournal.at(0)) {
        strlcpy(dashboardState.messageFrom, message->direction ? "You" : message->name, sizeof(dashboardState.messageFrom));
        strlcpy(dashboardState.messageText, message->text, sizeof(dashboardState.messageText));
    }
    if (meshReady) {
        const auto *prefs = coreMesh->getNodePrefs();
        dashboardState.frequency = prefs->freq;
        dashboardState.bandwidth = prefs->bw;
        dashboardState.sf = prefs->sf;
        dashboardState.noiseFloor = radio_driver.getNoiseFloor();
        if (prefs->node_lat != 0 || prefs->node_lon != 0) {
            strlcpy(dashboardState.position, "Saved", sizeof(dashboardState.position));
            snprintf(dashboardState.positionDetail, sizeof(dashboardState.positionDetail), "%.1f, %.1f", prefs->node_lat,
                     prefs->node_lon);
        }
        strlcpy(dashboardState.scope, prefs->default_scope_name, sizeof(dashboardState.scope));
    }
    dashboardState.wifiConnected = WiFi.status() == WL_CONNECTED;
    strlcpy(dashboardState.network, dashboardState.wifiConnected ? WiFi.SSID().c_str() : "Wi-Fi off",
            sizeof(dashboardState.network));
    if (hamViewModule) {
        const auto status = hamViewModule->getSnapshot(nullptr, 0);
        dashboardState.spots = status.spotCount;
        strlcpy(dashboardState.hamStatus, status.hamAlertConnected ? "Connected" : "Not connected",
                sizeof(dashboardState.hamStatus));
    }
    dashboard.update(dashboardState);
    dashboard.setNetworkDetail(CairnCompanion::connected() ? "BLE connected" : CairnCompanion::enabled() ? "BLE on" : "BLE off");
    if ((activePage == Page::Node || activePage == Page::Nodes) && viewStatus)
        lv_label_set_text(viewStatus, nodeStatus);
    if ((activePage == Page::Conversation || activePage == Page::History) && renderedRevision != messageJournal.revision())
        renderConversation();
    if (activePage == Page::Nodes && viewText)
        lv_label_set_text(viewText, contactsText().c_str());
    if (activePage == Page::Radio && runtimeStatus)
        lv_label_set_text_fmt(runtimeStatus, "TX / RX  |  %lu packets received\nNoise floor: %d dBm",
                              static_cast<unsigned long>(meshReady ? radio_driver.getPacketsRecv() : 0),
                              meshReady ? radio_driver.getNoiseFloor() : 0);
    if (activePage == Page::Network && runtimeStatus) {
        if (WiFi.status() == WL_CONNECTED)
            lv_label_set_text_fmt(runtimeStatus, "Connected: %s\n%s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        else
            lv_label_set_text(runtimeStatus, "Wi-Fi disconnected");
    }
    if (activePage == Page::WifiCompanion && runtimeStatus)
        lv_label_set_text_fmt(runtimeStatus, "%s\n%s : 5000",
                              CairnCompanion::wifiConnected() ? "Companion connected"
                              : CairnCompanion::wifiEnabled() ? "TCP companion enabled"
                                                              : "TCP companion disabled",
                              WiFi.localIP().toString().c_str());
    if (activePage == Page::ExportHistory && runtimeStatus)
        lv_label_set_text(runtimeStatus, CairnArchive::status());
}

void saveRadio(lv_event_t *)
{
    if (!meshReady)
        return;
    closeKeyboard();
    char *end = nullptr;
    const float frequency = strtof(lv_textarea_get_text(freqField), &end);
    const float bandwidth = BANDWIDTHS[lv_dropdown_get_selected(bwDropdown)];
    const char *scope = lv_textarea_get_text(scopeField);
    if (*scope == '#')
        ++scope;
    if (!std::isfinite(frequency) || !end || *end || frequency - bandwidth / 2000 < 902 || frequency + bandwidth / 2000 > 928 ||
        !scope[0]) {
        lv_label_set_text(viewStatus, "Check frequency (US 902-928 MHz) and region.");
        return;
    }
    for (const char *p = scope; *p; ++p) {
        if (!isalnum(static_cast<unsigned char>(*p)) && *p != '-' && *p != '_') {
            lv_label_set_text(viewStatus, "Region: use letters, numbers, - or _.");
            return;
        }
    }
    NodePrefs *prefs = coreMesh->getNodePrefs();
    const float oldFreq = prefs->freq, oldBw = prefs->bw;
    const uint8_t oldSf = prefs->sf, oldCr = prefs->cr;
    char oldScope[31];
    uint8_t oldKey[16];
    memcpy(oldScope, prefs->default_scope_name, sizeof(oldScope));
    memcpy(oldKey, prefs->default_scope_key, sizeof(oldKey));
    prefs->freq = frequency;
    prefs->bw = bandwidth;
    prefs->sf = lv_dropdown_get_selected(sfDropdown) + 5;
    prefs->cr = lv_dropdown_get_selected(crDropdown) + 5;
    strlcpy(prefs->default_scope_name, scope, sizeof(prefs->default_scope_name));
    TransportKeyStore keys;
    TransportKey key;
    const String hashtag = String("#") + scope;
    keys.getAutoKeyFor(0, hashtag.c_str(), key);
    memcpy(prefs->default_scope_key, key.key, sizeof(key.key));
    if (!storage->savePrefs(*prefs)) {
        prefs->freq = oldFreq;
        prefs->bw = oldBw;
        prefs->sf = oldSf;
        prefs->cr = oldCr;
        memcpy(prefs->default_scope_name, oldScope, sizeof(oldScope));
        memcpy(prefs->default_scope_key, oldKey, sizeof(oldKey));
        lv_label_set_text(viewStatus, "Could not save radio settings.");
        return;
    }
    prefs->clearDirty();
    radio_driver.applyParams(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
    const bool confirmed = CairnRadioSetup::confirm();
    radio_driver.setTxEnabled(confirmed);
    lv_label_set_text(viewStatus, confirmed ? "Radio settings saved. Transmission enabled."
                                            : "Radio settings saved; could not enable transmission. Save again to retry.");
}

void saveNetwork(lv_event_t *)
{
    closeKeyboard();
    const char *ssid = lv_textarea_get_text(ssidField);
    const char *password = lv_textarea_get_text(passwordField);
    if (!ssid[0]) {
        lv_label_set_text(viewStatus, "Enter a Wi-Fi network name.");
        return;
    }
    Preferences prefs;
    if (!prefs.begin("cairn_net", false)) {
        lv_label_set_text(viewStatus, "Could not save Wi-Fi settings.");
        return;
    }
    NetworkSettings settings;
    strlcpy(settings.ssid, ssid, sizeof(settings.ssid));
    strlcpy(settings.password, password, sizeof(settings.password));
    const bool saved = prefs.putBytes("wifi", &settings, sizeof(settings)) == sizeof(settings);
    prefs.end();
    if (!saved) {
        lv_label_set_text(viewStatus, "Could not save Wi-Fi settings.");
        return;
    }
    WiFi.begin(ssid, password);
    configTzTime(CairnClock::timezone(), "pool.ntp.org");
    lv_label_set_text(viewStatus, "Connecting to Wi-Fi...");
}

void savePosition(lv_event_t *)
{
    if (!meshReady)
        return;
    closeKeyboard();
    char *latEnd, *lonEnd;
    const double latitude = strtod(lv_textarea_get_text(latitudeField), &latEnd);
    const double longitude = strtod(lv_textarea_get_text(longitudeField), &lonEnd);
    if (!lv_textarea_get_text(latitudeField)[0] || !lv_textarea_get_text(longitudeField)[0] || *latEnd || *lonEnd ||
        !std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 ||
        longitude > 180) {
        lv_label_set_text(viewStatus, "Enter a valid latitude and longitude.");
        return;
    }
    auto *prefs = coreMesh->getNodePrefs();
    const double oldLatitude = prefs->node_lat, oldLongitude = prefs->node_lon;
    prefs->node_lat = latitude;
    prefs->node_lon = longitude;
    if (!storage->savePrefs(*prefs)) {
        prefs->node_lat = oldLatitude;
        prefs->node_lon = oldLongitude;
        lv_label_set_text(viewStatus, "Could not save position.");
        return;
    }
    sensors.node_lat = latitude;
    sensors.node_lon = longitude;
    prefs->clearDirty();
    lv_label_set_text(viewStatus, "Position saved.");
}

void displayPreference(lv_event_t *event)
{
    if (lv_obj_check_type(lv_event_get_target_obj(event), &lv_slider_class)) {
        brightness = lv_slider_get_value(lv_event_get_target_obj(event));
        display.setBrightness(brightness);
    } else {
        use12hClock = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    }
    Preferences prefs;
    if (prefs.begin("cairn_ui", false)) {
        prefs.putUChar("brightness", brightness);
        prefs.putBool("clock12", use12hClock);
        prefs.end();
    }
}

void showPage(Page page)
{
    CairnRadioTools::stop();
    scanChart = waterfall = nullptr;
    hamview_mui_close();
    closeKeyboard();
    activePage = page;
    viewStatus = runtimeStatus = viewText = nullptr;
    conversationLog = composeField = nullptr;
    lv_obj_clean(body);
    for (unsigned i = 0; i < CairnMap::TILES; ++i) {
        lv_image_cache_drop(&mapDescriptors[i]);
        mapDescriptors[i] = {};
        mapImages[i] = mapLoading[i] = nullptr;
    }
    CairnMap::close();
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    if (page == Page::Home) {
        shell.select(CairnAction::Home);
        lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(body);
    shell.select(page == Page::Chats                                   ? CairnAction::Chats
                 : page == Page::Conversation || page == Page::History ? CairnAction::Chats
                 : page == Page::Nodes || page == Page::Node           ? CairnAction::Nodes
                 : page == Page::Radio                                 ? CairnAction::Radio
                 : page == Page::Position                              ? CairnAction::Position
                                                                       : CairnAction::Settings);
    const char *titles[] = {"Home",      "Chats",           "Nodes",     "Radios",      "Network",       "Storage",
                            "Position",  "Settings",        "Hardware",  "Appearance",  "System",        "Files",
                            "File",      "Conversation",    "History",   "Node",        "Channel",       "Radio protocol",
                            "Bluetooth", "Wi-Fi companion", "Edit text", "Delete file", "New text file", "Spectrum / waterfall",
                            "Tools",     "Export history",  "Maps",      "Map source"};
    static_assert(sizeof(titles) / sizeof(titles[0]) == unsigned(Page::Count), "Page titles out of sync");
    lv_obj_t *title = label(body, titles[static_cast<int>(page)], TEAL);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    if (page == Page::Chats) {
        button(body, "Message history", [](lv_event_t *) { showPage(Page::History); });
        button(body, "Add / edit channel", [](lv_event_t *) { showPage(Page::ChannelSetup); });
        label(body, "CHANNELS", TEAL);
        unsigned channels = 0;
        ChannelDetails channel;
        for (unsigned i = 0; i < MAX_GROUP_CHANNELS; ++i)
            if (meshReady && coreMesh->getChannel(i, channel) && channel.name[0]) {
                ++channels;
                auto *row = button(
                    body, channel.name,
                    [](lv_event_t *event) {
                        directChat = false;
                        selectedChannel = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
                        showPage(Page::Conversation);
                    },
                    reinterpret_cast<void *>(uintptr_t(i)));
                lv_obj_set_width(row, LV_PCT(100));
            }
        if (!channels)
            label(body, "No channels configured.");
        label(body, "DIRECT MESSAGES", TEAL);
        unsigned contacts = 0;
        ContactInfo contact;
        for (unsigned i = MAX_ANON_CONTACTS; meshReady && coreMesh->getContactByIdx(i, contact); ++i) {
            if (!contact.name[0])
                continue;
            ++contacts;
            auto *row = button(
                body, contact.name,
                [](lv_event_t *event) {
                    const auto index = static_cast<unsigned>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
                    if (coreMesh->getContactByIdx(index, selectedContact)) {
                        directChat = true;
                        showPage(Page::Conversation);
                    }
                },
                reinterpret_cast<void *>(uintptr_t(i)));
            lv_obj_set_width(row, LV_PCT(100));
        }
        if (!contacts)
            label(body, "Listening for node advertisements.");
    } else if (page == Page::Conversation || page == Page::History) {
        if (page == Page::Conversation) {
            ChannelDetails channel{};
            if (!directChat)
                coreMesh->getChannel(selectedChannel, channel);
            lv_label_set_text(title, directChat ? selectedContact.name : channel.name);
        }
        conversationLog = lv_obj_create(body);
        surface(conversationLog, BG);
        lv_obj_set_size(conversationLog, LV_PCT(100), page == Page::History ? 330 : 160);
        lv_obj_set_style_pad_all(conversationLog, 0, 0);
        lv_obj_set_style_pad_row(conversationLog, 8, 0);
        lv_obj_set_flex_flow(conversationLog, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(conversationLog, LV_DIR_VER);
        if (page == Page::Conversation) {
            composeField = field(body, "Message", "", 148);
            lv_textarea_set_placeholder_text(composeField, "Write a message...");
            auto *counter = label(body, "0 / 148 bytes");
            lv_obj_add_event_cb(
                composeField,
                [](lv_event_t *event) {
                    const size_t bytes = strlen(lv_textarea_get_text(lv_event_get_target_obj(event)));
                    lv_label_set_text_fmt(static_cast<lv_obj_t *>(lv_event_get_user_data(event)), "%u / 148 bytes",
                                          unsigned(bytes));
                },
                LV_EVENT_VALUE_CHANGED, counter);
            auto *replies = lv_obj_create(body);
            surface(replies, BG);
            lv_obj_set_size(replies, LV_PCT(100), 46);
            lv_obj_set_style_pad_all(replies, 0, 0);
            lv_obj_set_flex_flow(replies, LV_FLEX_FLOW_ROW);
            for (const char *reply : {"Yes", "No", "Received"}) {
                auto *choice = button(
                    replies, reply,
                    [](lv_event_t *event) {
                        lv_textarea_set_text(composeField, static_cast<const char *>(lv_event_get_user_data(event)));
                    },
                    const_cast<char *>(reply));
                lv_obj_set_width(choice, 116);
            }
            auto *send = button(body, "Send", sendChat);
            lv_obj_set_width(send, LV_PCT(100));
        }
        viewStatus = label(body, messageJournal.healthy() ? "" : "History storage unavailable.");
        renderConversation();
    } else if (page == Page::Nodes) {
        button(body, "Discover nearby repeaters", discoverNodes);
        viewStatus = label(body, nodeStatus);
        ContactInfo contact;
        unsigned count = 0;
        for (unsigned i = MAX_ANON_CONTACTS; meshReady && coreMesh->getContactByIdx(i, contact); ++i) {
            if (!contact.name[0])
                continue;
            ++count;
            auto *row = button(
                body, contact.name,
                [](lv_event_t *event) {
                    const auto index = static_cast<unsigned>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
                    if (coreMesh->getContactByIdx(index, selectedContact))
                        showPage(Page::Node);
                },
                reinterpret_cast<void *>(uintptr_t(i)));
            lv_obj_set_width(row, LV_PCT(100));
        }
        if (!count)
            label(body, "No nodes discovered yet.");
    } else if (page == Page::Node) {
        lv_label_set_text(title, selectedContact.name);
        label(body, selectedContact.type == 2 ? "Repeater" : selectedContact.type == 3 ? "Room server" : "Chat node", TEAL);
        char key[65];
        mesh::Utils::toHex(key, selectedContact.id.pub_key, 32);
        label(body, key);
        label(body, selectedContact.out_path_len == OUT_PATH_UNKNOWN ? "Route unknown" : "Route available");
        if (selectedContact.gps_lat || selectedContact.gps_lon) {
            char coordinates[64];
            snprintf(coordinates, sizeof(coordinates), "%.6f, %.6f", selectedContact.gps_lat / 1000000.0,
                     selectedContact.gps_lon / 1000000.0);
            label(body, coordinates);
            button(body, "Show on map", [](lv_event_t *) {
                mapLatitude = selectedContact.gps_lat / 1000000.0;
                mapLongitude = selectedContact.gps_lon / 1000000.0;
                mapZoom = 13;
                mapPositioned = true;
                showPage(Page::Map);
            });
        }
        button(body, "Message", [](lv_event_t *) {
            directChat = true;
            showPage(Page::Conversation);
        });
        button(body, "Request route", [](lv_event_t *) {
            uint8_t command[34] = {52, 0};
            memcpy(command + 2, selectedContact.id.pub_key, 32);
            if (!localInterface.submit(command, sizeof(command), nodeResponse, nullptr))
                strlcpy(nodeStatus, "Radio command busy.", sizeof(nodeStatus));
        });
        button(body, "Request status", [](lv_event_t *) {
            uint8_t command[33] = {27};
            memcpy(command + 1, selectedContact.id.pub_key, 32);
            if (!localInterface.submit(command, sizeof(command), nodeResponse, nullptr))
                strlcpy(nodeStatus, "Radio command busy.", sizeof(nodeStatus));
        });
        if (selectedContact.type == 2 || selectedContact.type == 3) {
            loginField = field(body, "Repeater / room password", "", 64);
            lv_textarea_set_password_mode(loginField, true);
            button(body, "Log in", [](lv_event_t *) {
                const char *password = lv_textarea_get_text(loginField);
                uint8_t command[97] = {26};
                memcpy(command + 1, selectedContact.id.pub_key, 32);
                const size_t length = strlen(password);
                if (length > 64) {
                    strlcpy(nodeStatus, "Password must fit within 64 UTF-8 bytes.", sizeof(nodeStatus));
                    return;
                }
                memcpy(command + 33, password, length);
                const bool accepted = localInterface.submit(command, 33 + length, nodeResponse, nullptr);
                memset(command, 0, sizeof(command));
                lv_textarea_set_text(loginField, "");
                closeKeyboard();
                if (!accepted)
                    strlcpy(nodeStatus, "Radio command busy.", sizeof(nodeStatus));
            });
        }
        viewStatus = label(body, nodeStatus);
    } else if (page == Page::Protocol) {
        label(body, "Current: MeshCore");
        const bool direct = CairnBoot::resident();
        label(body, direct ? "Both firmwares and their separate data stay in internal flash."
                           : "Each protocol retains its own identity and filesystem on the SD card.");
        CairnBoot::Bank bank;
        meshtastic_SdCardInfo card;
        const bool available = !CairnArchive::active() &&
                               (direct ? CairnBoot::residentAvailable(CairnBoot::Meshtastic)
                                       : indicatorFiles.info(card) && card.present && !card.busy && !card.unformatted &&
                                             CairnBoot::loaderAvailable() && CairnBoot::loadBank(CairnBoot::Meshtastic, bank));
        auto *switchButton = button(body, "Switch to Meshtastic", [](lv_event_t *) {
            if (!CairnBoot::request(CairnBoot::Meshtastic))
                lv_label_set_text(viewStatus, "Switch unavailable. Check the installed profiles.");
        });
        lv_obj_set_width(switchButton, LV_PCT(100));
        if (!available)
            lv_obj_add_state(switchButton, LV_STATE_DISABLED);
        viewStatus = label(body, available && direct      ? "Ready. Switching restarts the device; no SD backup or restore."
                                 : available              ? "Ready. Switching takes several minutes; keep the SD card inserted."
                                 : CairnArchive::active() ? "Finish the export, then reopen this page."
                                                          : "Protocol switching is not provisioned.");
    } else if (page == Page::ChannelSetup) {
        channelSlot = dropdown(body, "Channel slot",
                               "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26"
                               "\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39",
                               1);
        channelNameField = field(body, "Channel name", "", 31);
        channelKeyField = field(body, "128-bit key (32 hexadecimal characters)", "", 32);
        lv_textarea_set_password_mode(channelKeyField, true);
        lv_textarea_set_accepted_chars(channelKeyField, "0123456789abcdefABCDEF");
        button(body, "Save channel", [](lv_event_t *) {
            const char *name = lv_textarea_get_text(channelNameField);
            const char *key = lv_textarea_get_text(channelKeyField);
            if (!name[0] || strlen(key) != 32) {
                lv_label_set_text(viewStatus, "Enter a name and a 32-character key.");
                return;
            }
            for (unsigned i = 0; i < 32; ++i)
                if (!isxdigit(static_cast<unsigned char>(key[i]))) {
                    lv_label_set_text(viewStatus, "The channel key must contain hexadecimal digits only.");
                    return;
                }
            uint8_t command[50] = {32, static_cast<uint8_t>(lv_dropdown_get_selected(channelSlot))};
            strlcpy(reinterpret_cast<char *>(command + 2), name, 32);
            for (unsigned i = 0; i < 16; ++i) {
                char pair[3] = {key[i * 2], key[i * 2 + 1], 0};
                command[34 + i] = strtoul(pair, nullptr, 16);
            }
            if (!localInterface.submit(
                    command, sizeof(command),
                    [](const uint8_t *response, size_t length, void *) {
                        if (activePage == Page::ChannelSetup && viewStatus)
                            lv_label_set_text(viewStatus,
                                              length && response[0] == 0 ? "Channel saved." : "Could not save channel.");
                    },
                    nullptr))
                lv_label_set_text(viewStatus, "Radio command busy.");
            memset(command, 0, sizeof(command));
            lv_textarea_set_text(channelKeyField, "");
            closeKeyboard();
        });
        viewStatus = label(body, "Existing slot contents are replaced when saved.");
    } else if (page == Page::Radio) {
        button(body, "Spectrum / waterfall", [](lv_event_t *) { showPage(Page::Spectrum); });
        label(body, "US radio profile. One default flood scope is active at a time.");
        runtimeStatus = label(body, "", AMBER);
        NodePrefs *prefs = coreMesh->getNodePrefs();
        char frequency[16];
        snprintf(frequency, sizeof(frequency), "%.3f", meshReady ? prefs->freq : LORA_FREQ);
        freqField = field(body, "Frequency (MHz)", frequency, 12);
        lv_textarea_set_accepted_chars(freqField, "0123456789.");
        uint32_t bw = 6;
        for (uint32_t i = 0; i < sizeof(BANDWIDTHS) / sizeof(BANDWIDTHS[0]); ++i)
            if (fabsf(prefs->bw - BANDWIDTHS[i]) < 0.02f)
                bw = i;
        bwDropdown = dropdown(body, "Bandwidth (kHz)", "7.8\n10.4\n15.6\n20.8\n31.25\n41.7\n62.5\n125\n250\n500", bw);
        sfDropdown =
            dropdown(body, "Spreading factor", "SF5\nSF6\nSF7\nSF8\nSF9\nSF10\nSF11\nSF12", meshReady ? prefs->sf - 5 : 2);
        crDropdown = dropdown(body, "Coding rate", "4/5\n4/6\n4/7\n4/8", meshReady ? prefs->cr - 5 : 0);
        scopeField = field(body, "Region / flood scope", meshReady ? prefs->default_scope_name : "us-ms", 30);
        lv_obj_t *scopeButtons = lv_obj_create(body);
        surface(scopeButtons, BG);
        lv_obj_set_size(scopeButtons, LV_PCT(100), 56);
        lv_obj_set_style_pad_all(scopeButtons, 0, 0);
        lv_obj_set_flex_flow(scopeButtons, LV_FLEX_FLOW_ROW);
        for (const char *scope : {"us", "us-ms"}) {
            auto *quick = button(
                scopeButtons, scope,
                [](lv_event_t *e) { lv_textarea_set_text(scopeField, static_cast<const char *>(lv_event_get_user_data(e))); },
                const_cast<char *>(scope));
            lv_obj_set_width(quick, 125);
        }
        auto *save = button(body, "Save radio profile", saveRadio);
        lv_obj_set_width(save, LV_PCT(100));
        if (!meshReady)
            lv_obj_add_state(save, LV_STATE_DISABLED);
        viewStatus = label(body,
                           CairnRadioSetup::confirmed() ? "MeshCore transmission enabled."
                                                        : "Receive only. Save your radio settings to enable transmission.",
                           AMBER);
    } else if (page == Page::Spectrum) {
        label(body, "902–928 MHz • receive only • 500 kHz measurement bandwidth");
        button(body, "Start / stop sweep", [](lv_event_t *) {
            if (CairnRadioTools::active()) {
                CairnRadioTools::stop();
                lv_label_set_text(viewStatus, "Stopped. Mesh radio restored.");
            } else {
                const auto *prefs = coreMesh->getNodePrefs();
                const bool started = meshReady && CairnRadioTools::start(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
                lv_label_set_text(viewStatus,
                                  started ? "Scanning. Mesh reception and sending are paused." : "Radio is busy; try again.");
            }
        });
        scanChart = lv_chart_create(body);
        surface(scanChart, CARD, 10);
        lv_obj_set_size(scanChart, LV_PCT(100), 130);
        lv_chart_set_type(scanChart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(scanChart, CairnRadioTools::BINS);
        lv_chart_set_range(scanChart, LV_CHART_AXIS_PRIMARY_Y, -140, -20);
        lv_obj_set_style_size(scanChart, 0, 0, LV_PART_INDICATOR);
        scanSeries = lv_chart_add_series(scanChart, lv_color_hex(TEAL), LV_CHART_AXIS_PRIMARY_Y);
        peakSeries = lv_chart_add_series(scanChart, lv_color_hex(0xFF35C4), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_all_value(scanChart, scanSeries, -140);
        lv_chart_set_all_value(scanChart, peakSeries, -140);
        label(body, "902                    915                    928 MHz");
        if (!waterfallPixels)
            waterfallPixels = static_cast<uint16_t *>(heap_caps_aligned_alloc(32, 384 * 80 * 2, MALLOC_CAP_SPIRAM));
        if (waterfallPixels) {
            memset(waterfallPixels, 0, 384 * 80 * 2);
            waterfall = lv_canvas_create(body);
            lv_canvas_set_buffer(waterfall, waterfallPixels, 384, 80, LV_COLOR_FORMAT_RGB565);
        }
        runtimeStatus = label(body, "Live RSSI: cyan / peak hold: pink", TEAL);
        viewStatus = label(body, "Leaving this screen restores the saved radio profile.");
    } else if (page == Page::Network) {
        runtimeStatus = label(body, "");
        const NetworkSettings settings = loadNetwork();
        ssidField = field(body, "Wi-Fi network", settings.ssid, 32);
        passwordField = field(body, "Wi-Fi password", settings.password, 63);
        lv_textarea_set_password_mode(passwordField, true);
        auto *connect = button(body, "Save and connect", saveNetwork);
        lv_obj_set_width(connect, LV_PCT(100));
        label(body, "Wi-Fi supplies HamView alerts and clock synchronization.");
        button(body, "Bluetooth companion", [](lv_event_t *) { showPage(Page::Bluetooth); });
        button(body, "Wi-Fi companion", [](lv_event_t *) { showPage(Page::WifiCompanion); });
        viewStatus = label(body, "");
    } else if (page == Page::WifiCompanion) {
        runtimeStatus = label(body, "", TEAL);
        label(body, "Enable only on a trusted LAN. Connected clients can change node settings.");
        label(body, "Enable TCP companion on port 5000");
        auto *enabled = lv_switch_create(body);
        if (CairnCompanion::wifiEnabled())
            lv_obj_add_state(enabled, LV_STATE_CHECKED);
        button(
            body, "Save and restart MeshCore",
            [](lv_event_t *event) {
                auto *choice = static_cast<lv_obj_t *>(lv_event_get_user_data(event));
                if (!CairnCompanion::saveWifi(lv_obj_has_state(choice, LV_STATE_CHECKED))) {
                    lv_label_set_text(viewStatus, "Could not save connection settings.");
                    return;
                }
                requestRestart();
            },
            enabled);
        viewStatus = label(body, "Disabled by default. Bluetooth pairing remains separate.");
    } else if (page == Page::Bluetooth) {
        label(body, CairnCompanion::name(), TEAL);
        label(body, CairnCompanion::connected() ? "Companion connected" : "Use the official MeshCore app to pair.");
        label(body, "Enable Bluetooth");
        auto *enabled = lv_switch_create(body);
        if (CairnCompanion::enabled())
            lv_obj_add_state(enabled, LV_STATE_CHECKED);
        char pin[8];
        snprintf(pin, sizeof(pin), "%06lu", (unsigned long)CairnCompanion::pin());
        auto *pinField = field(body, "Pairing PIN (6 digits)", pin, 6);
        lv_textarea_set_accepted_chars(pinField, "0123456789");
        auto **fields = new lv_obj_t *[2]{enabled, pinField};
        auto *save = button(
            body, "Save and restart MeshCore",
            [](lv_event_t *event) {
                auto **fields = static_cast<lv_obj_t **>(lv_event_get_user_data(event));
                const char *text = lv_textarea_get_text(fields[1]);
                if (strlen(text) != 6 ||
                    !CairnCompanion::save(lv_obj_has_state(fields[0], LV_STATE_CHECKED), strtoul(text, nullptr, 10))) {
                    lv_label_set_text(viewStatus, "Enter a PIN from 100000 to 999999.");
                    return;
                }
                requestRestart();
            },
            fields);
        lv_obj_add_event_cb(
            save, [](lv_event_t *event) { delete[] static_cast<lv_obj_t **>(lv_event_get_user_data(event)); }, LV_EVENT_DELETE,
            fields);
        viewStatus = label(body, "Bluetooth uses encrypted, PIN-protected pairing.");
    } else if (page == Page::Settings) {
        auto *grid = lv_obj_create(body);
        surface(grid, BG);
        lv_obj_set_style_pad_all(grid, 0, 0);
        lv_obj_set_size(grid, LV_PCT(100), 366);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_row(grid, 12, 0);
        lv_obj_set_style_pad_column(grid, 12, 0);
        const char *names[] = {"LoRa", "Network", "GNSS", "Hardware", "Appearance", "System"};
        const char *details[] = {"Frequency / scope", "Wi-Fi / clock",   "Saved position",
                                 "Display / storage", "Clock / display", "Firmware / status"};
        const Page routes[] = {Page::Radio, Page::Network, Page::Position, Page::Hardware, Page::Appearance, Page::System};
        const uint32_t accents[] = {0xFFE500, 0x00D7E6, 0x20FF60, 0xC15CFA, 0xFF35C4, 0xB6A9D2};
        for (unsigned i = 0; i < 6; ++i) {
            auto *tile = button(grid, "", navigation, reinterpret_cast<void *>(static_cast<uintptr_t>(routes[i])));
            lv_obj_set_size(tile, 184, 114);
            auto *name = label(tile, names[i], accents[i]);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
            lv_obj_set_pos(name, 0, 0);
            auto *detail = label(tile, details[i]);
            lv_obj_set_pos(detail, 0, 48);
        }
    } else if (page == Page::Map) {
        if (!CairnMap::begin()) {
            label(body, "Map service could not start.", AMBER);
        } else {
            if (!mapPositioned) {
                const auto *prefs = coreMesh->getNodePrefs();
                if (prefs->node_lat || prefs->node_lon) {
                    mapLatitude = prefs->node_lat;
                    mapLongitude = prefs->node_lon;
                    mapZoom = 12;
                }
                mapPositioned = true;
            }
            lv_image_cache_resize(2 * 1024 * 1024, true);
            auto *viewport = lv_obj_create(body);
            surface(viewport, CARD, 8);
            lv_obj_set_size(viewport, 384, 256);
            lv_obj_set_style_pad_all(viewport, 0, 0);
            lv_obj_remove_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_clip_corner(viewport, true, 0);
            double centerX, centerY;
            CairnMap::project(mapLatitude, mapLongitude, mapZoom, centerX, centerY);
            const double left = centerX - 192, top = centerY - 128;
            const int firstX = int(std::floor(left / 256)), firstY = int(std::floor(top / 256));
            for (unsigned i = 0; i < CairnMap::TILES; ++i) {
                const int column = i % 3, row = i / 3;
                mapImages[i] = lv_image_create(viewport);
                lv_obj_set_pos(mapImages[i], (firstX + column) * 256 - left, (firstY + row) * 256 - top);
                mapLoading[i] = lv_label_create(viewport);
                lv_label_set_text(mapLoading[i], "Loading map");
                lv_obj_set_pos(mapLoading[i], (firstX + column) * 256 - left + 30, (firstY + row) * 256 - top + 100);
                CairnMap::request(i, mapZoom, firstX + column, firstY + row);
            }
            const auto marker = [&](double latitude, double longitude, uint32_t color, unsigned index) {
                if ((!latitude && !longitude) || !std::isfinite(latitude) || !std::isfinite(longitude))
                    return;
                double x, y;
                CairnMap::project(latitude, longitude, mapZoom, x, y);
                x -= left;
                y -= top;
                const double world = 256.0 * (1 << mapZoom);
                if (x < -world / 2)
                    x += world;
                if (x > world / 2)
                    x -= world;
                if (x < 0 || x >= 384 || y < 0 || y >= 256)
                    return;
                auto *dot = lv_obj_create(viewport);
                surface(dot, color, 6);
                lv_obj_set_size(dot, 12, 12);
                lv_obj_set_pos(dot, x - 6, y - 6);
                lv_obj_set_ext_click_area(dot, 10);
                if (index != UINT32_MAX)
                    lv_obj_add_event_cb(
                        dot,
                        [](lv_event_t *event) {
                            const unsigned index = uintptr_t(lv_event_get_user_data(event));
                            if (coreMesh->getContactByIdx(index, selectedContact))
                                showPage(Page::Node);
                        },
                        LV_EVENT_CLICKED, reinterpret_cast<void *>(uintptr_t(index)));
            };
            const auto *prefs = coreMesh->getNodePrefs();
            marker(prefs->node_lat, prefs->node_lon, 0x20FF60, UINT32_MAX);
            ContactInfo contact;
            for (unsigned index = MAX_ANON_CONTACTS; meshReady && coreMesh->getContactByIdx(index, contact); ++index)
                marker(contact.gps_lat / 1000000.0, contact.gps_lon / 1000000.0, contact.type == 2 ? AMBER : TEAL, index);
            auto *credit = lv_label_create(viewport);
            lv_label_set_text(credit, CairnMap::attribution());
            lv_obj_set_width(credit, 376);
            lv_obj_set_style_text_font(credit, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(credit, lv_color_white(), 0);
            lv_obj_set_style_bg_color(credit, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(credit, LV_OPA_80, 0);
            lv_obj_align(credit, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
            auto *controls = lv_obj_create(body);
            surface(controls, BG);
            lv_obj_set_size(controls, LV_PCT(100), 46);
            lv_obj_set_style_pad_all(controls, 0, 0);
            lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
            const char *names[] = {"<", "^", "v", ">", "+", "-"};
            for (unsigned i = 0; i < 6; ++i) {
                auto *control = button(
                    controls, names[i],
                    [](lv_event_t *event) {
                        const unsigned action = uintptr_t(lv_event_get_user_data(event));
                        if (action >= 4) {
                            mapZoom = action == 4 ? std::min(18U, mapZoom + 1) : std::max(2U, mapZoom - 1);
                        } else {
                            double x, y;
                            CairnMap::project(mapLatitude, mapLongitude, mapZoom, x, y);
                            x += action == 0 ? -128 : action == 3 ? 128 : 0;
                            y += action == 1 ? -128 : action == 2 ? 128 : 0;
                            CairnMap::unproject(x, y, mapZoom, mapLatitude, mapLongitude);
                        }
                        showPage(Page::Map);
                    },
                    reinterpret_cast<void *>(uintptr_t(i)));
                lv_obj_set_width(control, 54);
            }
            runtimeStatus = label(body, CairnMap::status(), TEAL);
            button(body, "Map source", [](lv_event_t *) { showPage(Page::MapSettings); });
            button(body, "Position settings / center on me", [](lv_event_t *) {
                mapPositioned = false;
                showPage(Page::Position);
            });
        }
    } else if (page == Page::MapSettings) {
        CairnMap::begin();
        mapURL = field(body, "PNG tile URL template", CairnMap::provider(), 191);
        mapCredit = field(body, "Visible attribution", CairnMap::attribution(), 95);
        label(body, "Use {z}, {x}, {y} placeholders and 256x256 PNG tiles. Only viewed tiles are downloaded and cached.");
        button(body, "Save map source", [](lv_event_t *) {
            closeKeyboard();
            const bool saved = CairnMap::saveProvider(lv_textarea_get_text(mapURL), lv_textarea_get_text(mapCredit));
            lv_label_set_text(viewStatus,
                              saved ? "Map source saved." : "Check the URL placeholders, attribution and available storage.");
        });
        viewStatus = label(body, "Use a provider whose terms permit your intended use.");
        button(body, "Back to map", [](lv_event_t *) { showPage(Page::Map); });
    } else if (page == Page::Position) {
        label(body, "Saved location for your node. A GPS receiver can be added through the Grove connection.");
        auto *prefs = coreMesh->getNodePrefs();
        char latitude[24], longitude[24];
        snprintf(latitude, sizeof(latitude), "%.6f", prefs->node_lat);
        snprintf(longitude, sizeof(longitude), "%.6f", prefs->node_lon);
        latitudeField = field(body, "Latitude (-90 to 90)", latitude, 18);
        longitudeField = field(body, "Longitude (-180 to 180)", longitude, 18);
        lv_textarea_set_accepted_chars(latitudeField, "0123456789.-");
        lv_textarea_set_accepted_chars(longitudeField, "0123456789.-");
        auto *save = button(body, "Save position", savePosition);
        if (!meshReady)
            lv_obj_add_state(save, LV_STATE_DISABLED);
        viewStatus = label(body, "");
        button(body, "Open map", [](lv_event_t *) {
            mapPositioned = false;
            showPage(Page::Map);
        });
    } else if (page == Page::Hardware || page == Page::Appearance) {
        if (page == Page::Appearance)
            label(body, "Neon / Montserrat / 480 x 480", TEAL);
        label(body, "Display brightness");
        auto *slider = lv_slider_create(body);
        lv_obj_set_size(slider, LV_PCT(94), 28);
        lv_slider_set_range(slider, 10, 255);
        lv_slider_set_value(slider, brightness, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, displayPreference, LV_EVENT_RELEASED, nullptr);
        auto *displayMode = dropdown(body, "Clock display", "UTC\nLocal time", CairnClock::local() ? 1 : 0);
        lv_obj_add_event_cb(
            displayMode,
            [](lv_event_t *event) {
                CairnClock::save(lv_dropdown_get_selected(lv_event_get_target_obj(event)) != 0, CairnClock::zone());
                refresh(nullptr);
            },
            LV_EVENT_VALUE_CHANGED, nullptr);
        auto *timeZone = dropdown(body, "Local time zone", CairnClock::zoneOptions(), CairnClock::zone());
        lv_obj_add_event_cb(
            timeZone,
            [](lv_event_t *event) {
                CairnClock::save(CairnClock::local(), lv_dropdown_get_selected(lv_event_get_target_obj(event)));
                refresh(nullptr);
            },
            LV_EVENT_VALUE_CHANGED, nullptr);
        label(body, "12-hour clock");
        auto *clockSwitch = lv_switch_create(body);
        if (use12hClock)
            lv_obj_add_state(clockSwitch, LV_STATE_CHECKED);
        lv_obj_add_event_cb(clockSwitch, displayPreference, LV_EVENT_VALUE_CHANGED, nullptr);
        auto *files = button(body, "Storage", navigation, reinterpret_cast<void *>(static_cast<uintptr_t>(Page::Storage)));
        lv_obj_set_width(files, LV_PCT(100));
    } else if (page == Page::Tools) {
        const char *names[] = {"Spectrum / waterfall", "Files",    "Export messages",
                               "Connections",          "Position", "System / node name"};
        const Page routes[] = {Page::Spectrum, Page::Files, Page::ExportHistory, Page::Network, Page::Map, Page::System};
        for (unsigned i = 0; i < 6; ++i) {
            auto *action = button(body, names[i], navigation, reinterpret_cast<void *>(uintptr_t(routes[i])));
            lv_obj_set_width(action, LV_PCT(100));
        }
    } else if (page == Page::ExportHistory) {
        label(body, "Export the saved message snapshot as JSON Lines. Live messaging continues during export.");
        button(body, "Export to SD card", [](lv_event_t *) {
            CairnArchive::start();
            lv_label_set_text(runtimeStatus, CairnArchive::status());
        });
        runtimeStatus = label(body, CairnArchive::status(), TEAL);
        button(body, "Open files", [](lv_event_t *) {
            directoryOffset = 0;
            showPage(Page::Files);
        });
    } else if (page == Page::System) {
        char info[240];
        snprintf(info, sizeof(info), "SenseCAP Indicator\nMeshCore %s\n\nUptime: %lu s\nFree RAM: %lu KB\nFree PSRAM: %lu KB",
                 FIRMWARE_VERSION, static_cast<unsigned long>(Time::getUptimeSecs()),
                 static_cast<unsigned long>(ESP.getFreeHeap() / 1024), static_cast<unsigned long>(ESP.getFreePsram() / 1024));
        label(body, info, 0xF4F0FF);
        ownerField = field(body, "Node name", coreMesh->getNodePrefs()->node_name, 31);
        button(body, "Save node name", [](lv_event_t *) {
            const char *name = lv_textarea_get_text(ownerField);
            const size_t length = strlen(name);
            if (!length || length > 31) {
                lv_label_set_text(viewStatus, "Use a name from 1 to 31 UTF-8 bytes.");
                return;
            }
            uint8_t command[32]{8};
            memcpy(command + 1, name, length);
            closeKeyboard();
            if (!localInterface.submit(
                    command, length + 1,
                    [](const uint8_t *reply, size_t size, void *) {
                        if (activePage == Page::System && viewStatus)
                            lv_label_set_text(viewStatus, size && reply[0] == 0
                                                              ? "Name saved. Restart to refresh Bluetooth advertising."
                                                              : "Could not save node name.");
                    },
                    nullptr))
                lv_label_set_text(viewStatus, "Radio command busy.");
        });
        button(body, "Restart MeshCore", [](lv_event_t *) { requestRestart(); });
        viewStatus = label(body, "");
    } else if (page == Page::Storage) {
        meshtastic_SdCardInfo card{};
        if (indicatorFiles.info(card) && card.present) {
            char info[160];
            snprintf(info, sizeof(info), "SD card / %s\n%.2f GB total\n%.2f GB free%s",
                     card.fat_type == meshtastic_SdCardInfo_FatType_FAT32 ? "FAT32" : "FAT",
                     static_cast<double>(card.card_size) / 1000000000.0, static_cast<double>(card.free_bytes) / 1000000000.0,
                     card.stats_valid ? "" : " (scanning)");
            label(body, info, 0xF4F0FF);
            auto *files = button(body, "Files", navigation, reinterpret_cast<void *>(static_cast<uintptr_t>(Page::Files)));
            lv_obj_set_width(files, LV_PCT(100));
        } else {
            label(body, card.busy ? "Checking SD card..." : "SD card unavailable", AMBER);
        }
        for (const auto command : {meshtastic_SdCommand_SD_MOUNT, meshtastic_SdCommand_SD_EJECT}) {
            auto *action = button(
                body, command == meshtastic_SdCommand_SD_MOUNT ? "Mount / re-scan" : "Eject card",
                [](lv_event_t *e) {
                    meshtastic_SdCardInfo info{};
                    const auto command =
                        static_cast<meshtastic_SdCommand>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
                    indicatorFiles.command(command, info);
                    showPage(Page::Storage);
                },
                reinterpret_cast<void *>(static_cast<uintptr_t>(command)));
            lv_obj_set_width(action, LV_PCT(100));
            if (command == meshtastic_SdCommand_SD_EJECT && CairnArchive::active())
                lv_obj_add_state(action, LV_STATE_DISABLED);
        }
        if (meshReady) {
            char info[160];
            snprintf(info, sizeof(info),
                     "MeshCore storage\n%lu KB used / %lu KB total\n\nIdentity, contacts and radio profile are saved here.",
                     static_cast<unsigned long>(SPIFFS.usedBytes() / 1024),
                     static_cast<unsigned long>(SPIFFS.totalBytes() / 1024));
            label(body, info, 0xFFFFFF);
        } else {
            label(body, meshStatus, AMBER);
        }
    } else if (page == Page::Files) {
        label(body, directoryPath.c_str(), TEAL);
        button(body, "New text file", [](lv_event_t *) { showPage(Page::FileNew); });
        auto *up = button(body, "Parent folder", [](lv_event_t *) {
            if (directoryPath.length() > 1) {
                directoryPath.remove(directoryPath.length() - 1);
                directoryPath = directoryPath.substring(0, directoryPath.lastIndexOf('/') + 1);
            }
            directoryOffset = 0;
            showPage(Page::Files);
        });
        lv_obj_set_width(up, LV_PCT(100));
        if (indicatorFiles.list(directoryPath.c_str(), directoryOffset, fileListing)) {
            for (unsigned i = 0; i < fileListing.filenames_count; ++i) {
                const String name(fileListing.filenames[i]);
                auto *path = new String(directoryPath + name);
                if (path->length() > 255) {
                    delete path;
                    continue;
                }
                auto *entry = button(
                    body, name.c_str(),
                    [](lv_event_t *e) {
                        const String selected = *static_cast<String *>(lv_event_get_user_data(e));
                        if (selected.endsWith("/")) {
                            directoryPath = selected;
                            directoryOffset = 0;
                            showPage(Page::Files);
                        } else {
                            selectedFile = selected;
                            showPage(Page::File);
                        }
                    },
                    path);
                lv_obj_set_size(entry, LV_PCT(100), 48);
                lv_obj_add_event_cb(
                    entry, [](lv_event_t *e) { delete static_cast<String *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, path);
            }
            if (!fileListing.total_count)
                label(body, "This folder is empty");
            if (directoryOffset)
                button(body, "Previous", [](lv_event_t *) {
                    directoryOffset = directoryOffset > 16 ? directoryOffset - 16 : 0;
                    showPage(Page::Files);
                });
            if (directoryOffset + fileListing.filenames_count < fileListing.total_count)
                button(body, "Next", [](lv_event_t *) {
                    directoryOffset += fileListing.filenames_count;
                    showPage(Page::Files);
                });
        } else {
            label(body, "Could not read this folder", AMBER);
        }
    } else if (page == Page::File) {
        label(body, selectedFile.c_str(), TEAL);
        uint32_t got, size;
        if (indicatorFiles.read(selectedFile.c_str(), 0, fileText, 4096, got, size)) {
            char details[64];
            snprintf(details, sizeof(details), "%lu bytes", static_cast<unsigned long>(size));
            label(body, details);
            String name = selectedFile;
            name.toLowerCase();
            if (name.endsWith(".txt") || name.endsWith(".json") || name.endsWith(".jsonl") || name.endsWith(".log") ||
                name.endsWith(".csv") || name.endsWith(".md") || name.endsWith(".ini")) {
                for (unsigned i = 0; i < got; ++i)
                    if (fileText[i] < 0x20 && fileText[i] != '\n' && fileText[i] != '\t')
                        fileText[i] = ' ';
                fileText[got] = 0;
                label(body, reinterpret_cast<const char *>(fileText), 0xF4F0FF);
                if (size <= 4096 && !protectedFile(selectedFile))
                    button(body, "Edit text", [](lv_event_t *) { showPage(Page::FileEdit); });
            }
            if (!protectedFile(selectedFile))
                button(body, "Delete file", [](lv_event_t *) { showPage(Page::FileDelete); });
            else
                label(body, "Protected or active file: read only.", AMBER);
        } else {
            label(body, "Could not open this file", AMBER);
        }
        button(body, "Back to files", navigation, reinterpret_cast<void *>(static_cast<uintptr_t>(Page::Files)));
    } else if (page == Page::FileEdit) {
        uint32_t got, size;
        if (protectedFile(selectedFile) || !indicatorFiles.read(selectedFile.c_str(), 0, fileText, 4096, got, size) ||
            size > 4096 || memchr(fileText, 0, got)) {
            label(body, "Only ordinary text files up to 4096 bytes can be edited.", AMBER);
        } else {
            fileText[got] = 0;
            label(body, selectedFile.c_str(), TEAL);
            fileEditor = field(body, "Contents", reinterpret_cast<const char *>(fileText), 4096);
            lv_textarea_set_one_line(fileEditor, false);
            lv_obj_set_height(fileEditor, 210);
            button(body, "Save file", [](lv_event_t *) {
                const char *text = lv_textarea_get_text(fileEditor);
                const size_t size = strlen(text);
                closeKeyboard();
                if (size > 4096 || protectedFile(selectedFile)) {
                    lv_label_set_text(viewStatus, "Text must fit within 4096 UTF-8 bytes.");
                    return;
                }
                lv_label_set_text(
                    viewStatus, indicatorFiles.write(selectedFile.c_str(), 0, reinterpret_cast<const uint8_t *>(text), size, true)
                                    ? "Saved."
                                    : "Could not save file.");
            });
            viewStatus = label(body, "");
        }
        button(body, "Back", [](lv_event_t *) { showPage(Page::File); });
    } else if (page == Page::FileDelete) {
        label(body, selectedFile.c_str(), TEAL);
        label(body, "Delete this file from the SD card?");
        button(body, "Delete permanently", [](lv_event_t *) {
            if (!protectedFile(selectedFile) && indicatorFiles.remove(selectedFile.c_str()))
                showPage(Page::Files);
            else
                lv_label_set_text(viewStatus, "Could not delete file.");
        });
        button(body, "Cancel", [](lv_event_t *) { showPage(Page::File); });
        viewStatus = label(body, "");
    } else if (page == Page::FileNew) {
        label(body, directoryPath.c_str(), TEAL);
        newFileName = field(body, "File name", "note.txt", 64);
        lv_textarea_set_accepted_chars(newFileName, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.");
        button(body, "Create text file", [](lv_event_t *) {
            const String name(lv_textarea_get_text(newFileName));
            if (!name.length() || name.length() > 64 || name == "." || name == ".." || name.endsWith(".") || name.endsWith(" ") ||
                name[0] == ' ' ||
                strspn(name.c_str(), "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.") != name.length()) {
                lv_label_set_text(viewStatus, "Enter a file name without folder separators.");
                return;
            }
            const String path = directoryPath + name;
            if (path.length() > 255 || protectedFile(path)) {
                lv_label_set_text(viewStatus, "This name is reserved for recovery files.");
                return;
            }
            for (uint32_t offset = 0;;) {
                if (!indicatorFiles.list(directoryPath.c_str(), offset, fileListing)) {
                    lv_label_set_text(viewStatus, "Could not check this folder.");
                    return;
                }
                for (unsigned i = 0; i < fileListing.filenames_count; ++i)
                    if (name.equalsIgnoreCase(fileListing.filenames[i])) {
                        lv_label_set_text(viewStatus, "That file already exists.");
                        return;
                    }
                offset += fileListing.filenames_count;
                if (offset >= fileListing.total_count)
                    break;
                if (!fileListing.filenames_count) {
                    lv_label_set_text(viewStatus, "Folder listing incomplete.");
                    return;
                }
            }
            if (!indicatorFiles.write(path.c_str(), 0, reinterpret_cast<const uint8_t *>(""), 0, true)) {
                lv_label_set_text(viewStatus, "Could not create file.");
                return;
            }
            closeKeyboard();
            selectedFile = path;
            showPage(Page::FileEdit);
        });
        viewStatus = label(body, "Existing files will not be replaced.");
    }
    refresh(nullptr);
}

void navigation(lv_event_t *event)
{
    showPage(static_cast<Page>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))));
}

void dashboardAction(CairnAction action)
{
    ++touchEvents;
    if (action == CairnAction::HamView) {
        closeKeyboard();
        hamview_mui_open();
        return;
    }
    switch (action) {
    case CairnAction::Home:
        showPage(Page::Home);
        break;
    case CairnAction::Clock:
        showPage(Page::Appearance);
        break;
    case CairnAction::Chats:
        showPage(Page::Chats);
        break;
    case CairnAction::Nodes:
        showPage(Page::Nodes);
        break;
    case CairnAction::Radio:
        showPage(Page::Radio);
        break;
    case CairnAction::Position:
        showPage(Page::Map);
        break;
    case CairnAction::Network:
        showPage(Page::Network);
        break;
    case CairnAction::Files:
        showPage(Page::Files);
        break;
    case CairnAction::Tools:
        showPage(Page::Tools);
        break;
    default:
        showPage(Page::Settings);
        break;
    }
}

class MessageListener : public MyMesh::Listener
{
    void add(const char *from, const char *text, uint32_t timestamp, const uint8_t *peer, uint8_t channel)
    {
        messageJournal.add(from, text, timestamp, peer, channel, false);
    }
    void onMessageRecv(mesh::Packet *, const ContactInfo &from, uint8_t, uint32_t timestamp, const char *text) override
    {
        add(from.name, text, timestamp, from.id.pub_key, 255);
    }
    void onChannelMessageRecv(mesh::Packet *, ChannelDetails &channel, const char *text) override
    {
        add(channel.name, text, rtc_clock.getCurrentTime(), nullptr, coreMesh->findChannelIdx(channel.channel));
    }
    void onACKRecv(uint32_t ack) override { messageJournal.acknowledge(ack); }
    void onQueueSizeChanged(int) override {}
};
MessageListener listener;
} // namespace

void cairnMuiInstall()
{
    dashboard.create(home, dashboardAction);
}

bool coreUiBegin(MyMesh &core, DataStore &store)
{
    coreMesh = &core;
    storage = &store;
    if (!display.init())
        return false;
    display.setColorDepth(16);
    screenCapture = static_cast<uint16_t *>(heap_caps_calloc(480 * 480, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    Preferences uiPrefs;
    if (uiPrefs.begin("cairn_ui", true)) {
        brightness = uiPrefs.getUChar("brightness", 180);
        use12hClock = uiPrefs.getBool("clock12", false);
        uiPrefs.end();
    }
    display.setBrightness(brightness);
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    lv_display_t *lcd = lv_display_create(480, 480);
    constexpr size_t bytes = 480 * 40 * 2;
    void *buffer = heap_caps_aligned_alloc(32, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lcd || !buffer)
        return false;
    lv_display_set_color_format(lcd, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(lcd, buffer, nullptr, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(lcd, [](lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
        if (screenCapture) {
            const size_t width = lv_area_get_width(area);
            for (int y = area->y1; y <= area->y2; ++y)
                memcpy(screenCapture + y * 480 + area->x1, pixels + (y - area->y1) * width * 2, width * 2);
        }
        display.pushImage(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area),
                          reinterpret_cast<const lgfx::rgb565_t *>(pixels));
        lv_display_flush_ready(disp);
    });
    lv_theme_t *theme = lv_theme_default_init(lcd, lv_color_hex(TEAL), lv_color_hex(0xB8A0F2), true, &lv_font_montserrat_16);
    lv_display_set_theme(lcd, theme);
    lv_indev_t *touch = pointerInput = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch, lcd);
    lv_indev_set_read_cb(touch, [](lv_indev_t *, lv_indev_data_t *data) {
        ++touchReads;
        if (touchOverride) {
            data->point.x = touchX;
            data->point.y = touchY;
            data->state = touchPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
            if (!touchPressed)
                touchOverride = false;
            return;
        }
        uint16_t x, y;
        const bool pressed = display.getTouchXY(&x, &y);
        data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        if (pressed) {
            data->point.x = x;
            data->point.y = y;
        }
    });
    root = lv_screen_active();
    shell.create(root, dashboardAction);
    auto *modes = button(shell.header, "Modes", [](lv_event_t *) { showPage(Page::Protocol); });
    lv_obj_set_pos(modes, 274, 10);
    lv_obj_set_size(modes, 76, 25);
    lv_obj_set_style_text_font(modes, &lv_font_montserrat_14, 0);
    home = shell.home;
    body = shell.content;
    lv_obj_set_style_pad_all(body, 12, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    header = lv_obj_create(root);
    lv_obj_add_flag(header, LV_OBJ_FLAG_HIDDEN);
    keyboard = lv_keyboard_create(root);
    lv_obj_set_size(keyboard, 416, 220);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, [](lv_event_t *) { closeKeyboard(); }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(keyboard, [](lv_event_t *) { closeKeyboard(); }, LV_EVENT_CANCEL, nullptr);
    new HamViewModule();
    hamview_mui_install(root, header, home);
    if (!concurrency::OSThread::startAll())
        return false;
    core.setListener(&listener);
    localInterface.setObserver(observeMeshFrame);
    lv_timer_create(refresh, 1000, nullptr);
    initialized = true;
    refresh(nullptr);
    lv_timer_handler();
    return true;
}

void coreUiStartNetwork()
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    sntp_set_time_sync_notification_cb([](timeval *) { CairnClock::markTimeSynchronized(); });
    const NetworkSettings settings = loadNetwork();
    if (settings.ssid[0]) {
        WiFi.begin(settings.ssid, settings.password);
        configTzTime(CairnClock::timezone(), "pool.ntp.org");
    }
}

void coreUiSetReady(bool ready, const char *status)
{
    meshReady = ready;
    radio_driver.setTxEnabled(ready);
    meshStatus = status;
    if (initialized)
        refresh(nullptr);
    if (initialized && ready)
        CairnBoot::markHealthy(CairnBoot::MeshCore);
}

void coreUiLoop()
{
    unsigned tile;
    while (CairnMap::poll(tile)) {
        if (activePage == Page::Map && mapImages[tile]) {
            if (CairnMap::data(tile)) {
                auto &descriptor = mapDescriptors[tile];
                descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
                descriptor.header.cf = LV_COLOR_FORMAT_RAW;
                descriptor.header.w = descriptor.header.h = 256;
                descriptor.data_size = CairnMap::size(tile);
                descriptor.data = CairnMap::data(tile);
                lv_image_set_src(mapImages[tile], &descriptor);
                lv_obj_add_flag(mapLoading[tile], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_label_set_text(mapLoading[tile], "Tile unavailable");
            }
            if (runtimeStatus)
                lv_label_set_text(runtimeStatus, CairnMap::status());
        }
    }
    CairnArchive::tick();
    const int bin = CairnRadioTools::tick();
    if (bin >= 0 && activePage == Page::Spectrum && scanChart) {
        const int rssi = CairnRadioTools::value(bin);
        lv_chart_set_value_by_id(scanChart, scanSeries, bin, rssi);
        lv_chart_set_value_by_id(scanChart, peakSeries, bin, CairnRadioTools::peak(bin));
        lv_label_set_text_fmt(runtimeStatus, "%.3f MHz  %d dBm  peak %d", 902.0 + bin * 0.5, rssi, CairnRadioTools::peak(bin));
        if (waterfall && waterfallPixels) {
            if (bin == 0) {
                memmove(waterfallPixels, waterfallPixels + 384 * 5, 384 * 75 * 2);
                memset(waterfallPixels + 384 * 75, 0, 384 * 5 * 2);
            }
            const unsigned level = std::max(0, std::min(100, rssi + 130));
            const unsigned red = level > 60 ? (level - 60) * 255 / 40 : 0;
            const unsigned green = level * 255 / 100;
            const unsigned blue = 160 - level * 120 / 100;
            const uint16_t color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
            for (unsigned y = 75; y < 80; ++y)
                for (unsigned x = bin * 7; x < unsigned(bin + 1) * 7; ++x)
                    waterfallPixels[y * 384 + x] = color;
            lv_obj_invalidate(waterfall);
        }
    }
    if (restartPending && !CairnArchive::active() && Throttle::hasElapsed(restartRequested, 1000)) {
        CairnClock::checkpointTime();
        ESP.restart();
    }
    if (initialized) {
        if (meshReady && Throttle::hasElapsed(lastSignalSample, 500)) {
            lastSignalSample = Time::getMillis();
            if (dashboardState.samples == 120) {
                memmove(dashboardState.signal, dashboardState.signal + 1, 119 * sizeof(int16_t));
                --dashboardState.samples;
            }
            dashboardState.signal[dashboardState.samples++] = static_cast<int16_t>(radio_driver.getCurrentRSSI());
        }
        lv_timer_handler();
    }
}

bool coreUiCopyPixels(unsigned offset, unsigned length, unsigned char *out)
{
    constexpr unsigned size = 480 * 480 * 2;
    if (!initialized || !screenCapture || offset > size || length > size - offset)
        return false;
    if (!captureSnapshot)
        captureSnapshot = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!captureSnapshot)
        return false;
    if (offset == 0) {
        lv_refr_now(lv_display_get_default());
        memcpy(captureSnapshot, screenCapture, size);
    }
    memcpy(out, captureSnapshot + offset, length);
    return true;
}

bool coreUiTouch(unsigned x, unsigned y, bool pressed)
{
    if (!initialized || !pointerInput || x >= 480 || y >= 480)
        return false;
    touchX = x;
    touchY = y;
    touchPressed = pressed;
    touchOverride = true;
    lv_indev_read(pointerInput);
    return true;
}

unsigned coreUiInputDiagnostics(char *out, unsigned capacity)
{
    lv_point_t point{32, 148};
    lv_obj_update_layout(root);
    auto *hit = lv_indev_search_obj(root, &point);
    auto *top = lv_indev_search_obj(lv_layer_top(), &point);
    auto *sys = lv_indev_search_obj(lv_layer_sys(), &point);
    const auto *latest = messageJournal.at(0);
    return snprintf(out, capacity,
                    "{\"page\":%u,\"reads\":%lu,\"actions\":%lu,\"state\":%u,\"hit\":%u,"
                    "\"hitAction\":%u,\"topHit\":%u,\"sysHit\":%u,\"sameDisplay\":%u,"
                    "\"journalCount\":%u,\"journalHealthy\":%u,\"lastMessageId\":%lu,\"lastMessageStatus\":%u,"
                    "\"lastMessageChecksum\":%lu}",
                    unsigned(activePage), (unsigned long)touchReads, (unsigned long)touchEvents,
                    unsigned(lv_indev_get_state(pointerInput)), hit != nullptr,
                    hit ? unsigned(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(hit))) : 999, top != nullptr, sys != nullptr,
                    lv_indev_get_display(pointerInput) == lv_display_get_default(), unsigned(messageJournal.count()),
                    messageJournal.healthy(), (unsigned long)(latest ? latest->id : 0), latest ? unsigned(latest->status) : 0,
                    (unsigned long)(latest ? latest->checksum : 0));
}

bool coreUiPanelPixel(unsigned x, unsigned y, unsigned short &rendered, unsigned short &panel)
{
    if (!initialized || !screenCapture || x >= 480 || y >= 480)
        return false;
    lv_refr_now(lv_display_get_default());
    rendered = screenCapture[y * 480 + x];
    panel = display.readPixel(x, y);
    return true;
}

bool coreUiSetTime(unsigned epoch)
{
    const timeval tv{static_cast<time_t>(epoch), 0};
    if (settimeofday(&tv, nullptr) != 0)
        return false;
    rtc_clock.setCurrentTime(epoch);
    return true;
}

bool coreUiManagementAllowed(unsigned)
{
    return true;
}

unsigned coreUiClockPreview(unsigned epoch, char *out, unsigned capacity)
{
    char clock[16], date[64], zone[12];
    const time_t value = epoch ? epoch : time(nullptr);
    CairnClock::format(value, use12hClock, clock, sizeof(clock), date, sizeof(date), zone, sizeof(zone));
    return snprintf(
        out, capacity,
        "{\"clock\":\"%s\",\"date\":\"%s\",\"zone\":\"%s\",\"local\":%s,\"zoneIndex\":%u,\"epoch\":%lu,\"handoff\":%u}", clock,
        date, zone, CairnClock::local() ? "true" : "false", CairnClock::zone(), (unsigned long)value,
        CairnClock::handoffStatus());
}

static lv_obj_t *findKeyboard(lv_obj_t *parent)
{
    if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN))
        return nullptr;
    if (lv_obj_check_type(parent, &lv_keyboard_class))
        return lv_keyboard_get_textarea(parent);
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i)
        if (auto *field = findKeyboard(lv_obj_get_child(parent, i)))
            return field;
    return nullptr;
}

bool coreUiSetText(const char *text)
{
    if (!initialized)
        return false;
    auto *field = findKeyboard(root);
    if (!field)
        return false;
    lv_textarea_set_text(field, text);
    return true;
}
