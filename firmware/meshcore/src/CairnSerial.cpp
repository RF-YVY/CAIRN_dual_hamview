#include "CairnSerial.h"
#include "CairnCoreUI.h"
#include "UptimeClock.h"
#include "graphics/CairnBoot.h"
#include "graphics/CairnClock.h"
#include "mesh/Throttle.h"
#include <Preferences.h>
#if !defined(CAIRN_BOOTLOADER)
#include <WiFi.h>
#endif
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>

namespace
{
uint16_t get16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}
uint32_t get32(const uint8_t *p)
{
    return get16(p) | (static_cast<uint32_t>(get16(p + 2)) << 16);
}
void put16(uint8_t *p, uint16_t value)
{
    p[0] = value;
    p[1] = value >> 8;
}
void put32(uint8_t *p, uint32_t value)
{
    put16(p, value);
    put16(p + 2, value >> 16);
}
void put64(uint8_t *p, uint64_t value)
{
    put32(p, value);
    put32(p + 4, value >> 32);
}
uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    while (length--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}
void yieldStorage()
{
    if (esp_task_wdt_status(nullptr) == ESP_OK)
        esp_task_wdt_reset();
    delay(1);
}
} // namespace

void CairnSerial::begin(Stream &stream)
{
    io = &stream;
    if (!rx)
        rx = static_cast<uint8_t *>(heap_caps_malloc(CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!tx)
        tx = static_cast<uint8_t *>(heap_caps_malloc(CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!scratch)
        scratch = static_cast<uint8_t *>(heap_caps_malloc(SCRATCH_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    enable();
}

size_t CairnSerial::writeFrame(const uint8_t *bytes, size_t length)
{
    if (!enabled || !io || length > MAX_FRAME_SIZE)
        return 0;
    uint8_t header[] = {'>', static_cast<uint8_t>(length), static_cast<uint8_t>(length >> 8)};
    io->write(header, sizeof(header));
    return io->write(bytes, length);
}

size_t CairnSerial::checkRecvFrame(uint8_t *out)
{
    if (!enabled || !io)
        return 0;
    if (state && Throttle::hasElapsed(lastByte, 2000)) {
        state = 0;
        discard = 0;
    }
    while (io->available()) {
        const int byte = io->read();
        if (byte < 0)
            break;
        lastByte = Time::getMillis();
        if (discard) {
            --discard;
            continue;
        }
        switch (state) {
        case 0:
            if (byte == '<') {
                vendor = false;
                state = 2;
            } else if (byte == 0x94) {
                vendor = true;
                state = 1;
            }
            break;
        case 1:
            state = byte == 0xC3 ? 2 : 0;
            break;
        case 2:
            expected = vendor ? byte << 8 : byte;
            state = 3;
            break;
        case 3:
            expected |= vendor ? byte : byte << 8;
            received = 0;
            if (!expected || expected > (vendor ? CAPACITY : MAX_FRAME_SIZE)) {
                discard = expected;
                state = 0;
            } else {
                state = 4;
            }
            break;
        case 4:
            rx[received++] = byte;
            if (received == expected) {
                state = 0;
                if (vendor) {
                    if (expected >= 11 && crc32(rx, expected - 4) == get32(rx + expected - 4))
                        handle(expected - 4);
                } else {
                    memcpy(out, rx, expected);
                    return expected;
                }
            }
            break;
        }
    }
    return 0;
}

void CairnSerial::serviceOnly()
{
    uint8_t ignored[MAX_FRAME_SIZE];
    checkRecvFrame(ignored);
}

void CairnSerial::reply(uint8_t operation, uint32_t id, bool ok, unsigned bodyLength)
{
    if (bodyLength > CAPACITY - 10)
        return;
    tx[0] = operation | 0x80;
    put32(tx + 1, id);
    tx[5] = ok ? 0 : 1;
    put32(tx + 6 + bodyLength, crc32(tx, 6 + bodyLength));
    const unsigned length = 10 + bodyLength;
    const uint8_t header[] = {0x94, 0xC3, static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length)};
    io->write(header, sizeof(header));
    io->write(tx, length);
}

void CairnSerial::handle(size_t length)
{
    const uint8_t op = rx[0];
    const uint32_t id = get32(rx + 1);
    if (!coreUiManagementAllowed(op)) {
        reply(op, id, false);
        return;
    }
    const unsigned pathLength = get16(rx + 5);
    if (pathLength > 255 || 7 + pathLength > length) {
        reply(op, id, false);
        return;
    }
    char path[256];
    memcpy(path, rx + 7, pathLength);
    path[pathLength] = 0;
    if (strlen(path) != pathLength ||
        (pathLength && (path[0] != '/' || strstr(path, "/../") || (pathLength >= 3 && !strcmp(path + pathLength - 3, "/.."))))) {
        reply(op, id, false);
        return;
    }
    const uint8_t *body = rx + 7 + pathLength;
    const unsigned size = length - 7 - pathLength;
    bool ok = false;
    unsigned resultLength = 0;
    switch (op) {
    case 0: {
        put16(tx + 6, 1);
        put64(tx + 8, ESP.getEfuseMac());
        put16(tx + 16, CAPACITY);
        ok = true;
        resultLength = 12;
        break;
    }
    case 1: {
        meshtastic_SdCardInfo info{};
        ok = indicatorFiles.info(info);
        if (ok) {
            tx[6] = info.present;
            tx[7] = info.card_type;
            tx[8] = info.fat_type;
            tx[9] = info.stats_valid | (info.busy << 1) | (info.unformatted << 2);
            put64(tx + 10, info.card_size);
            put64(tx + 18, info.used_bytes);
            put64(tx + 26, info.free_bytes);
            resultLength = 28;
        }
        break;
    }
    case 2:
        if (size == 6) {
            const unsigned wanted = get16(body + 4);
            uint32_t got, total;
            ok = wanted && wanted <= 4096 && indicatorFiles.read(path, get32(body), tx + 12, wanted, got, total);
            if (ok) {
                put32(tx + 6, total);
                put16(tx + 10, got);
                resultLength = 6 + got;
            }
        }
        break;
    case 3:
        if (size >= 5 && size - 5 <= 4096) {
            const uint32_t offset = get32(body);
            const unsigned count = size - 5;
            ok = offset <= UINT32_MAX - count && indicatorFiles.write(path, offset, body + 5, count, body[4] != 0);
            if (ok) {
                put32(tx + 6, offset + count);
                resultLength = 4;
            }
        }
        break;
    case 4:
        if (size == 4 && indicatorFiles.list(path, get32(body), directory)) {
            put32(tx + 6, directory.total_count);
            put16(tx + 10, directory.filenames_count);
            resultLength = 6;
            for (unsigned i = 0; i < directory.filenames_count; ++i) {
                const unsigned n = strnlen(directory.filenames[i], 255);
                put16(tx + 6 + resultLength, n);
                resultLength += 2;
                memcpy(tx + 6 + resultLength, directory.filenames[i], n);
                resultLength += n;
            }
            ok = true;
        }
        break;
    case 5:
        ok = pathLength > 1 && indicatorFiles.remove(path);
        break;
    case 6:
        if (size == 1 && body[0] >= 1 && body[0] <= 3) {
            meshtastic_SdCardInfo info{};
            ok = indicatorFiles.command(static_cast<meshtastic_SdCommand>(body[0]), info);
        }
        break;
    case 7:
        if (size == 4) {
            const auto requested = get32(body);
            ok = requested == 115200 || requested == 460800 || requested == 921600;
            reply(op, id, ok);
            if (ok) {
                io->flush();
                delay(10);
                Serial.updateBaudRate(requested);
                baud = requested;
            }
            return;
        }
        break;
    case 8: {
        uint32_t offset = 0, total = 0, got = 0;
        mbedtls_sha256_context context;
        mbedtls_sha256_init(&context);
        ok = mbedtls_sha256_starts(&context, 0) == 0;
        do {
            if (!ok || !indicatorFiles.read(path, offset, scratch, SCRATCH_CAPACITY, got, total) || (!got && offset < total)) {
                ok = false;
                break;
            }
            ok = mbedtls_sha256_update(&context, scratch, got) == 0;
            offset += got;
            yieldStorage();
        } while (offset < total);
        if (ok) {
            put32(tx + 6, total);
            ok = mbedtls_sha256_finish(&context, tx + 10) == 0;
            resultLength = 36;
        }
        mbedtls_sha256_free(&context);
        break;
    }
    case 9:
        if (size == 6) {
            const unsigned offset = get32(body), count = get16(body + 4), flashSize = ESP.getFlashChipSize();
            ok = count <= 4096 && offset <= flashSize && count <= flashSize - offset &&
                 esp_flash_read(nullptr, tx + 6, offset, count) == ESP_OK;
            if (ok)
                resultLength = count;
        }
        break;
    case 10:
        if (size == 10) {
            uint32_t offset = get32(body), remaining = get32(body + 4);
            ok = remaining <= 1024 * 1024 && offset <= UINT32_MAX - remaining;
            memset(scratch, body[8], SCRATCH_CAPACITY);
            bool create = body[9];
            while (ok && remaining) {
                const unsigned count = remaining < SCRATCH_CAPACITY ? remaining : SCRATCH_CAPACITY;
                ok = indicatorFiles.write(path, offset, scratch, count, create);
                create = false;
                offset += count;
                remaining -= count;
                yieldStorage();
            }
            if (ok) {
                put32(tx + 6, offset);
                resultLength = 4;
            }
        }
        break;
#if !defined(CAIRN_BOOTLOADER)
    case 11:
        if (size >= 2 && body[0] <= 32 && body[1] <= 63 && size == 2u + body[0] + body[1] && body[0]) {
            struct {
                char ssid[33] = {};
                char password[64] = {};
            } settings;
            memcpy(settings.ssid, body + 2, body[0]);
            memcpy(settings.password, body + 2 + body[0], body[1]);
            Preferences prefs;
            if (prefs.begin("cairn_net", false)) {
                ok = prefs.putBytes("wifi", &settings, sizeof(settings)) == sizeof(settings);
                prefs.end();
                if (ok) {
                    WiFi.begin(settings.ssid, settings.password);
                    configTzTime(CairnClock::timezone(), "pool.ntp.org");
                }
            }
        }
        break;
#endif
    case 12:
        if (size == 4 && get32(body) >= 1577836800) {
            ok = coreUiSetTime(get32(body));
        }
        break;
    case 13:
        if (size == 6) {
            const unsigned count = get16(body + 4);
            ok = count <= 4096 && coreUiCopyPixels(get32(body), count, tx + 6);
            if (ok)
                resultLength = count;
        }
        break;
    case 14:
        ok = size == 5 && coreUiTouch(get16(body), get16(body + 2), body[4]);
        break;
    case 15:
        if (size <= 255) {
            char text[256];
            memcpy(text, body, size);
            text[size] = 0;
            ok = coreUiSetText(text);
        }
        break;
    case 16:
        put32(tx + 6, Time::getUptimeSecs());
        put32(tx + 10, ESP.getFreeHeap());
        put32(tx + 14, ESP.getMinFreeHeap());
        put32(tx + 18, ESP.getMaxAllocHeap());
        put32(tx + 22, ESP.getFreePsram());
        put32(tx + 26, baud);
        ok = true;
        resultLength = 24;
        break;
    case 26:
        if (size == 1) {
            CairnBoot::Bank bank;
            ok = CairnBoot::loadBank(body[0], bank);
            if (ok) {
                memcpy(tx + 6, &bank, sizeof(bank));
                resultLength = sizeof(bank);
            }
        }
        break;
    case 27:
        if (size == 4) {
            resultLength = coreUiClockPreview(get32(body), reinterpret_cast<char *>(tx + 6), 4096);
            ok = resultLength > 0 && resultLength < 4096;
        }
        break;
    case 23: {
        CairnBoot::State state;
        ok = CairnBoot::readState(state);
        if (ok) {
            memcpy(tx + 6, &state, sizeof(state));
            resultLength = sizeof(state);
        }
    } break;
    case 24:
        if (size == sizeof(CairnBoot::Bank)) {
            CairnBoot::Bank bank;
            memcpy(&bank, body, sizeof(bank));
            ok = CairnBoot::saveBank(bank);
        }
        break;
    case 25:
        if (size == sizeof(CairnBoot::Loader)) {
            CairnBoot::Loader loader;
            memcpy(&loader, body, sizeof(loader));
            ok = CairnBoot::saveLoader(loader) && CairnBoot::loaderAvailable();
        }
        break;
    case 19:
        if (size == 4) {
            unsigned short rendered, panel;
            ok = coreUiPanelPixel(get16(body), get16(body + 2), rendered, panel);
            if (ok) {
                put16(tx + 6, rendered);
                put16(tx + 8, panel);
                resultLength = 4;
            }
        }
        break;
    case 18:
        resultLength = coreUiInputDiagnostics(reinterpret_cast<char *>(tx + 6), 4096);
        ok = resultLength < 4096;
        break;
    case 17:
        ok = size == 1 && body[0] == 0xA5;
        reply(op, id, ok);
        if (ok) {
            io->flush();
            CairnClock::checkpointTime();
            delay(100);
            ESP.restart();
        }
        return;
    }
    reply(op, id, ok, ok ? resultLength : 0);
}
