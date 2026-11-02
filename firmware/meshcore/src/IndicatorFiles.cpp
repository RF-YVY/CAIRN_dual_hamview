#include "IndicatorFiles.h"
#include <HardwareSerial.h>
#if defined(CAIRN_CORE_PROFILE)
#include <mutex>
static std::mutex indicatorFilesMutex;
#define FILES_GUARD std::lock_guard<std::mutex> guard(indicatorFilesMutex)
#else
#define FILES_GUARD
#endif

IndicatorFiles indicatorFiles;
static HardwareSerial indicatorPort(2);

bool IndicatorFiles::begin()
{
    FILES_GUARD;
    if (!sensecapIndicator)
        sensecapIndicator = new SensecapIndicator(indicatorPort);
    return sensecapIndicator->wait_ready(2500);
}

bool IndicatorFiles::info(meshtastic_SdCardInfo &info)
{
    FILES_GUARD;
    return sensecapIndicator && sensecapIndicator->sd_info(&info);
}

bool IndicatorFiles::command(meshtastic_SdCommand command, meshtastic_SdCardInfo &info)
{
    FILES_GUARD;
    return sensecapIndicator && sensecapIndicator->sd_command(command, &info);
}

bool IndicatorFiles::read(const char *path, uint32_t offset, uint8_t *buffer, uint32_t length, uint32_t &received, uint32_t &size)
{
    FILES_GUARD;
    received = size = 0;
    if (!sensecapIndicator || length > sizeof(result.filedata.bytes))
        return false;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        memset(&result, 0, sizeof(result));
        if (sensecapIndicator->file_read(path, offset, length, &result) && result.status == meshtastic_FileStatus_FILE_OK) {
            if (result.offset != offset || result.filedata.size > length || result.file_size > UINT32_MAX)
                return false;
            received = result.filedata.size;
            size = result.file_size;
            memcpy(buffer, result.filedata.bytes, received);
            return true;
        }
        if (result.status != meshtastic_FileStatus_FILE_BUSY && result.status != meshtastic_FileStatus_FILE_UNSPECIFIED)
            return false;
        delay(100);
    }
    return false;
}

bool IndicatorFiles::write(const char *path, uint32_t offset, const uint8_t *buffer, uint32_t length, bool create)
{
    FILES_GUARD;
    if (!sensecapIndicator || length > sizeof(result.filedata.bytes))
        return false;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        memset(&result, 0, sizeof(result));
        if (sensecapIndicator->file_write(path, offset, buffer, length, create, &result)) {
            if (result.status == meshtastic_FileStatus_FILE_OK)
                return true;
            if (attempt && !create && result.status == meshtastic_FileStatus_FILE_OFFSET_CONFLICT &&
                result.file_size == static_cast<uint64_t>(offset) + length)
                return true;
        }
        if (result.status != meshtastic_FileStatus_FILE_BUSY && result.status != meshtastic_FileStatus_FILE_UNSPECIFIED)
            return false;
        delay(100);
    }
    return false;
}

bool IndicatorFiles::remove(const char *path)
{
    FILES_GUARD;
    return sensecapIndicator && sensecapIndicator->file_remove(path, &result) && result.status == meshtastic_FileStatus_FILE_OK;
}

bool IndicatorFiles::list(const char *path, uint32_t offset, meshtastic_DirectoryListing &result)
{
    FILES_GUARD;
    return sensecapIndicator && sensecapIndicator->list_directory(path, offset, &result) &&
           result.status == meshtastic_FileStatus_FILE_OK;
}
