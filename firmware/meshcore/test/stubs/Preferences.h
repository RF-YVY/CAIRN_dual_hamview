#pragma once
#include "Arduino.h"
#include <map>
#include <string>
#include <vector>
class Preferences
{
    std::string space;
    inline static std::map<std::string, std::vector<uint8_t>> values;

  public:
    bool begin(const char *name, bool)
    {
        space = name;
        return true;
    }
    void end() {}
    size_t getBytesLength(const char *key) { return values[space + key].size(); }
    size_t getBytes(const char *key, void *out, size_t size)
    {
        auto &bytes = values[space + key];
        if (bytes.size() != size)
            return 0;
        memcpy(out, bytes.data(), size);
        return size;
    }
    size_t putBytes(const char *key, const void *data, size_t size)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        values[space + key] = std::vector<uint8_t>(bytes, bytes + size);
        return size;
    }
};
