#pragma once
#include "Arduino.h"
#include <limits>
#include <vector>
#define FILE_READ "r"
inline std::vector<uint8_t> disk;
inline size_t nextWriteLimit = std::numeric_limits<size_t>::max();
inline bool fileExists = false;
class File
{
    bool valid = false;
    size_t position = 0;

  public:
    File(bool open = false) : valid(open) {}
    explicit operator bool() const { return valid; }
    size_t size() const { return disk.size(); }
    bool seek(size_t offset)
    {
        if (offset > disk.size())
            return false;
        position = offset;
        return true;
    }
    size_t read(uint8_t *out, size_t length)
    {
        if (!valid)
            return 0;
        length = min(length, disk.size() - position);
        memcpy(out, disk.data() + position, length);
        position += length;
        return length;
    }
    size_t write(const uint8_t *bytes, size_t length)
    {
        if (!valid)
            return 0;
        length = min(length, nextWriteLimit);
        nextWriteLimit = std::numeric_limits<size_t>::max();
        disk.resize(max(disk.size(), position + length));
        memcpy(disk.data() + position, bytes, length);
        position += length;
        return length;
    }
    void flush() {}
    void close() { valid = false; }
};
struct TestFS {
    bool exists(const char *) const { return fileExists; }
    File open(const char *, const char *mode)
    {
        if (*mode == 'w') {
            disk.clear();
            fileExists = true;
        }
        return File(fileExists);
    }
};
inline TestFS SPIFFS;
