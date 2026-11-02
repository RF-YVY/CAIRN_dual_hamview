#include "HamViewModule.h"
#include "UptimeClock.h"
#include "configuration.h"
#include "mesh/Throttle.h"

#if defined(HAMVIEW_MUI) && defined(ARCH_ESP32)

#include <Preferences.h>
#include <WiFi.h>
#include <ctype.h>

HamViewModule *hamViewModule = nullptr;
static void copyText(char *dst, size_t dstSize, const char *src)
{
    if (!dst || dstSize == 0)
        return;
    strlcpy(dst, src ? src : "", dstSize);
}

HamViewModule::HamViewModule() : concurrency::OSThread("HamView")
{
    hamViewModule = this;
    mutex = xSemaphoreCreateMutex();
    inputLine.reserve(1024);
    loadSettings();
}

HamViewModule::~HamViewModule()
{
    client.stop();
    if (mutex)
        vSemaphoreDelete(mutex);
    if (hamViewModule == this)
        hamViewModule = nullptr;
}

void HamViewModule::loadSettings()
{
    Preferences prefs;
    if (!prefs.begin("hamview", false))
        return;
    copyText(settings.username, sizeof(settings.username), prefs.getString("user", "").c_str());
    copyText(settings.telnetPassword, sizeof(settings.telnetPassword), prefs.getString("telpass", "").c_str());
    copyText(settings.telnetHost, sizeof(settings.telnetHost), prefs.getString("host", "hamalert.org").c_str());
    settings.telnetPort = prefs.getUShort("port", 7300);
    copyText(settings.callsignFilter, sizeof(settings.callsignFilter), prefs.getString("call", "").c_str());
    settings.spotTtlMinutes = prefs.getUShort("ttl", 30);
    settings.showSpotBanner = prefs.getBool("banner", true);
    if (prefs.isKey("pass"))
        prefs.remove("pass");
    prefs.end();
}

void HamViewModule::getSettings(HamViewSettings &out)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    out = settings;
    xSemaphoreGive(mutex);
}

void HamViewModule::saveSettings(const HamViewSettings &newSettings)
{
    HamViewSettings saved;
    xSemaphoreTake(mutex, portMAX_DELAY);
    settings = newSettings;
    settings.spotTtlMinutes = constrain(settings.spotTtlMinutes, 1, 1440);
    if (!settings.telnetHost[0])
        copyText(settings.telnetHost, sizeof(settings.telnetHost), "hamalert.org");
    if (!settings.telnetPort)
        settings.telnetPort = 7300;
    saved = settings;
    xSemaphoreGive(mutex);

    Preferences prefs;
    if (prefs.begin("hamview", false)) {
        prefs.putString("user", saved.username);
        prefs.putString("telpass", saved.telnetPassword);
        prefs.putString("host", saved.telnetHost);
        prefs.putUShort("port", saved.telnetPort);
        prefs.putString("call", saved.callsignFilter);
        prefs.putUShort("ttl", saved.spotTtlMinutes);
        prefs.putBool("banner", saved.showSpotBanner);
        prefs.remove("pass");
        prefs.end();
    }
    reconnectRequested = true;
    setIntervalFromNow(100);
}

HamViewStatus HamViewModule::getSnapshot(HamViewSpot *out, size_t capacity)
{
    HamViewStatus status;
    xSemaphoreTake(mutex, portMAX_DELAY);
    status.wifiConnected = WiFi.status() == WL_CONNECTED;
    status.hamAlertConnected = authenticated && client.connected();
    status.spotCount = static_cast<uint8_t>(spotCount);
    status.spotSequence = spotSequence;
    status.showSpotBanner = settings.showSpotBanner;
    copyText(status.message, sizeof(status.message), statusMessage);
    const size_t count = min(capacity, spotCount);
    if (out && count)
        memcpy(out, spots, count * sizeof(HamViewSpot));
    xSemaphoreGive(mutex);
    return status;
}

void HamViewModule::disconnect(const char *reason)
{
    client.stop();
    xSemaphoreTake(mutex, portMAX_DELAY);
    authenticated = false;
    inputLine = "";
    copyText(statusMessage, sizeof(statusMessage), reason);
    xSemaphoreGive(mutex);
}

bool HamViewModule::waitForServer(const char *expected, uint32_t timeoutMs)
{
    const uint32_t started = millis();
    String response;
    response.reserve(512);
    while (client.connected() && Throttle::isWithinTimespanMs(started, timeoutMs)) {
        while (client.available()) {
            response += static_cast<char>(client.read());
            if (HamViewProtocol::containsIgnoreCase(response.c_str(), "Login failed") ||
                HamViewProtocol::containsIgnoreCase(response.c_str(), "Database error"))
                return false;
            if (HamViewProtocol::containsIgnoreCase(response.c_str(), expected))
                return true;
            if (response.length() > 512)
                response.remove(0, 256);
        }
        delay(20);
    }
    return false;
}

bool HamViewModule::connectAndLogin()
{
    HamViewSettings current;
    getSettings(current);
    if (!current.username[0] || !current.telnetPassword[0]) {
        disconnect("Configure HamAlert Telnet login");
        return false;
    }
    disconnect("Connecting to HamAlert Telnet...");
    if (!client.connect(current.telnetHost, current.telnetPort, 5000)) {
        disconnect("Telnet connection failed");
        return false;
    }
    if (!waitForServer("login:", 5000)) {
        disconnect("HamAlert login prompt unavailable");
        return false;
    }
    client.println(current.username);
    if (!waitForServer("password:", 5000)) {
        disconnect("HamAlert password prompt unavailable");
        return false;
    }
    client.println(current.telnetPassword);
    if (!waitForServer("Hello ", 5000)) {
        disconnect("HamAlert login failed or timed out");
        return false;
    }
    client.println("set/json");
    if (!waitForServer("Operation successful", 5000)) {
        disconnect("HamAlert JSON setup failed");
        return false;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    authenticated = true;
    copyText(statusMessage, sizeof(statusMessage), "Telnet connected; waiting for spots");
    xSemaphoreGive(mutex);
    lastKeepalive = millis();
    LOG_INFO("HamView Telnet connection established\n");
    return true;
}

void HamViewModule::processJsonLine(const char *line)
{
    HamViewSpot spot;
    if (!HamViewProtocol::parseSpot(line, spot))
        return;
    spot.receivedAt = Time::getUptimeSecs();

    HamViewSettings current;
    getSettings(current);
    if (current.callsignFilter[0] && !HamViewProtocol::containsIgnoreCase(spot.callsign, current.callsignFilter) &&
        !HamViewProtocol::containsIgnoreCase(spot.comment, current.callsignFilter))
        return;

    xSemaphoreTake(mutex, portMAX_DELAY);
    const size_t moveCount = min(spotCount, MAX_SPOTS - 1);
    if (moveCount)
        memmove(&spots[1], &spots[0], moveCount * sizeof(HamViewSpot));
    spots[0] = spot;
    spotCount = min(spotCount + 1, MAX_SPOTS);
    ++spotSequence;
    xSemaphoreGive(mutex);
}

void HamViewModule::consumeInput()
{
    while (client.available()) {
        const char c = static_cast<char>(client.read());
        if (c == '\n') {
            inputLine.trim();
            processJsonLine(inputLine.c_str());
            inputLine = "";
        } else if (c != '\r') {
            if (inputLine.length() < 2048)
                inputLine += c;
            else
                inputLine = "";
        }
    }
}

void HamViewModule::pruneExpired()
{
    const uint32_t now = Time::getUptimeSecs();
    xSemaphoreTake(mutex, portMAX_DELAY);
    const uint32_t configuredTtl = settings.spotTtlMinutes * 60UL;
    const uint32_t ttl = configuredTtl < 60 ? 60 : configuredTtl;
    size_t kept = 0;
    for (size_t i = 0; i < spotCount; ++i) {
        if (now - spots[i].receivedAt <= ttl)
            spots[kept++] = spots[i];
    }
    spotCount = kept;
    xSemaphoreGive(mutex);
}

int32_t HamViewModule::runOnce()
{
    if (reconnectRequested) {
        reconnectRequested = false;
        disconnect("Settings saved; reconnecting");
    }
    if (WiFi.status() != WL_CONNECTED) {
        if (client.connected() || authenticated)
            disconnect("Enable Wi-Fi in MUI settings");
        return 2000;
    }
    if (!client.connected() || !authenticated) {
        connectAndLogin();
        return authenticated ? 100 : 5000;
    }
    consumeInput();
    if (Throttle::hasElapsed(lastKeepalive, 120000)) {
        client.println("echo cairn");
        lastKeepalive = millis();
    }
    pruneExpired();
    return 100;
}

#endif
