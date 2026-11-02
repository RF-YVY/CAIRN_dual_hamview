#include "CairnBoot.h"
#include "CairnClock.h"
#if defined(CAIRN_MUI) || defined(CAIRN_BOOTLOADER)
#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

namespace CairnBoot
{
static bool read(const char *key, void *out, size_t size)
{
    Preferences prefs;
    if (!prefs.begin("cairn_boot", true))
        return false;
    const bool valid = prefs.getBytesLength(key) == size && prefs.getBytes(key, out, size) == size;
    prefs.end();
    return valid;
}

static bool write(const char *key, const void *data, size_t size)
{
    Preferences prefs;
    if (!prefs.begin("cairn_boot", false))
        return false;
    const bool saved = prefs.putBytes(key, data, size) == size;
    prefs.end();
    return saved;
}

bool readState(State &state)
{
    return read("state", &state, sizeof(state)) && state.magic == MAGIC && state.active <= MeshCore && state.target <= MeshCore &&
           state.previous <= MeshCore && state.phase <= Booting;
}

bool saveState(const State &state)
{
    return state.magic == MAGIC && state.active <= MeshCore && state.target <= MeshCore && state.previous <= MeshCore &&
           state.phase <= Booting && write("state", &state, sizeof(state));
}

bool loadBank(uint8_t profile, Bank &bank)
{
    return profile <= MeshCore && read(profile == Meshtastic ? "bank0" : "bank1", &bank, sizeof(bank)) && bank.magic == MAGIC &&
           bank.version == 1 && bank.profile == profile && bank.generation < 2 && bank.appSize >= 256 &&
           bank.appSize <= 0x5C0000 && bank.filesystemSize == 0x180000;
}

bool saveBank(const Bank &bank)
{
    return bank.magic == MAGIC && bank.version == 1 && bank.profile <= MeshCore && bank.generation < 2 && bank.appSize >= 256 &&
           bank.appSize <= 0x5C0000 && bank.filesystemSize == 0x180000 &&
           write(bank.profile == Meshtastic ? "bank0" : "bank1", &bank, sizeof(bank));
}

void bankPath(char *out, unsigned capacity, const Bank &bank, bool filesystem)
{
    snprintf(out, capacity, "/cairn-%s-%s-%u.bin", bank.profile == Meshtastic ? "mt" : "mc", filesystem ? "fs" : "app",
             bank.generation);
}

bool saveLoader(const Loader &loader)
{
    return loader.magic == MAGIC && loader.size >= 256 && loader.size <= 0xA0000 && write("loader", &loader, sizeof(loader));
}

static const esp_partition_t *residentPartition(uint8_t profile)
{
    if (profile > MeshCore)
        return nullptr;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    profile == Meshtastic ? ESP_PARTITION_SUBTYPE_APP_OTA_0 : ESP_PARTITION_SUBTYPE_APP_OTA_1,
                                    profile == Meshtastic ? "app0" : "meshcore");
}

bool resident()
{
    return residentPartition(Meshtastic) && residentPartition(MeshCore) &&
           esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "mcfs");
}

bool residentAvailable(uint8_t profile)
{
    const auto *partition = residentPartition(profile);
    esp_app_desc_t description;
    return resident() && partition && esp_ota_get_partition_description(partition, &description) == ESP_OK;
}

bool loaderAvailable()
{
    Loader loader;
    if (!read("loader", &loader, sizeof(loader)) || loader.magic != MAGIC || loader.size < 256 || loader.size > 0xA0000)
        return false;
    const auto *partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, "flashApp");
    if (!partition || loader.size > partition->size)
        return false;
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    int error = mbedtls_sha256_starts(&hash, 0);
    uint8_t buffer[1024], digest[32];
    for (uint32_t offset = 0; !error && offset < loader.size; offset += sizeof(buffer)) {
        const size_t count = min(size_t(loader.size - offset), sizeof(buffer));
        error = esp_partition_read(partition, offset, buffer, count);
        if (!error)
            error = mbedtls_sha256_update(&hash, buffer, count);
        delay(1);
    }
    if (!error)
        error = mbedtls_sha256_finish(&hash, digest);
    mbedtls_sha256_free(&hash);
    return !error && !memcmp(digest, loader.hash, sizeof(digest));
}

bool request(uint8_t profile)
{
    State state;
    Bank bank;
    if (resident()) {
        const auto *target = residentPartition(profile);
        const auto *running = esp_ota_get_running_partition();
        if (!target || !running || target->address == running->address || !residentAvailable(profile))
            return false;
        state.active = state.previous = profile == Meshtastic ? MeshCore : Meshtastic;
        state.target = profile;
        state.phase = Booting;
        if (!saveState(state))
            return false;
        if (esp_ota_set_boot_partition(target) != ESP_OK) {
            state.target = state.active;
            state.phase = Idle;
            saveState(state);
            return false;
        }
        CairnClock::checkpointTime();
        ESP.restart();
        return true;
    }
    if (profile > MeshCore || !readState(state) || state.phase != Idle || state.active == profile || !loadBank(profile, bank) ||
        !loaderAvailable())
        return false;
    const auto *partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, "flashApp");
    state.target = profile;
    state.previous = state.active;
    state.phase = Requested;
    state.recovery = 0;
    if (!saveState(state))
        return false;
    if (esp_ota_set_boot_partition(partition) != ESP_OK) {
        state.phase = Idle;
        saveState(state);
        return false;
    }
    CairnClock::checkpointTime();
    ESP.restart();
    return true;
}

bool markHealthy(uint8_t profile)
{
    if (profile > MeshCore)
        return false;
    const auto *running = esp_ota_get_running_partition();
    const bool direct = resident();
    const auto *expected = direct ? residentPartition(profile) : nullptr;
    if (!running ||
        (direct ? !expected || running->address != expected->address : running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0))
        return false;
    State state;
    const bool existed = readState(state);
    if (existed && state.phase != Idle &&
        (state.phase != Booting || (state.target != profile && !(direct && state.previous == profile))))
        return false;
    if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK)
        return false;
    if (existed && state.phase == Idle && state.active == profile)
        return true;
    state.active = state.target = state.previous = profile;
    state.phase = Idle;
    state.recovery = 0;
    return saveState(state);
}
} // namespace CairnBoot

extern "C" const esp_partition_t *__real_esp_ota_get_next_update_partition(const esp_partition_t *start);
extern "C" const esp_partition_t *__wrap_esp_ota_get_next_update_partition(const esp_partition_t *start)
{
    // The other OTA slot contains the other protocol, not a disposable update bank.
    return CairnBoot::resident() ? nullptr : __real_esp_ota_get_next_update_partition(start);
}
#endif
