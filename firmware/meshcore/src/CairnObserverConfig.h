#pragma once
#include <cstddef>
#include <cstdint>

namespace CairnObserver
{
struct Configuration {
    uint8_t enabled = 0, tls = 0;
    uint16_t port = 1883;
    char region[17] = {};
    char host[96] = {};
    char username[128] = {};
    char password[512] = {};
    char topic[192] = "meshcore/{region}/{pubkey}/{type}";
    uint8_t reserved = 0;
};
constexpr size_t boundedLength(const char *text, size_t capacity)
{
    size_t length = 0;
    while (length < capacity && text[length])
        ++length;
    return length;
}
constexpr bool startsWith(const char *text, const char *prefix)
{
    for (size_t i = 0; prefix[i]; ++i)
        if (text[i] != prefix[i])
            return false;
    return true;
}
constexpr bool hasToken(const char *text, const char *token)
{
    for (size_t i = 0; text[i]; ++i)
        if (startsWith(text + i, token))
            return true;
    return false;
}
constexpr bool appendText(char *out, size_t capacity, size_t &used, const char *value)
{
    for (size_t i = 0; value[i]; ++i) {
        if (used + 1 >= capacity)
            return false;
        out[used++] = value[i];
    }
    return true;
}
constexpr bool formatTopic(const Configuration &value, const char *key, const char *type, char *out, size_t capacity)
{
    if (!capacity)
        return false;
    out[0] = 0;
    if (boundedLength(value.topic, sizeof(value.topic)) == sizeof(value.topic))
        return false;
    size_t used = 0;
    for (size_t i = 0; value.topic[i];) {
        const char *replacement = nullptr;
        size_t tokenLength = 0;
        if (startsWith(value.topic + i, "{region}")) {
            replacement = value.region;
            tokenLength = 8;
        } else if (startsWith(value.topic + i, "{pubkey}")) {
            replacement = key;
            tokenLength = 8;
        } else if (startsWith(value.topic + i, "{type}")) {
            replacement = type;
            tokenLength = 6;
        }
        if (replacement) {
            if (!appendText(out, capacity, used, replacement))
                return false;
            i += tokenLength;
        } else {
            const unsigned char c = value.topic[i++];
            if (c < 32 || c == 127 || c == '#' || c == '+' || c == '{' || c == '}' || used + 1 >= capacity)
                return false;
            out[used++] = c;
        }
    }
    out[used] = 0;
    return used > 0;
}
constexpr bool validConfiguration(const Configuration &value)
{
    if (value.enabled > 1 || value.tls > 1 || boundedLength(value.region, sizeof(value.region)) == sizeof(value.region) ||
        boundedLength(value.host, sizeof(value.host)) == sizeof(value.host) ||
        boundedLength(value.username, sizeof(value.username)) == sizeof(value.username) ||
        boundedLength(value.password, sizeof(value.password)) == sizeof(value.password) ||
        boundedLength(value.topic, sizeof(value.topic)) == sizeof(value.topic))
        return false;
    for (size_t i = 0; value.region[i]; ++i) {
        const char c = value.region[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }
    if (!value.enabled)
        return true;
    if (!value.host[0] || !value.port || !hasToken(value.topic, "{type}") ||
        (hasToken(value.topic, "{region}") && !value.region[0]))
        return false;
    for (size_t i = 0; value.host[i]; ++i) {
        const char c = value.host[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':'))
            return false;
    }
    constexpr char KEY[] = "0000000000000000000000000000000000000000000000000000000000000000";
    char topic[384] = {};
    return formatTopic(value, KEY, "packets", topic, sizeof(topic)) && formatTopic(value, KEY, "status", topic, sizeof(topic));
}
} // namespace CairnObserver
