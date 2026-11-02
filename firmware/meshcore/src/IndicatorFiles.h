#pragma once

#include "mesh/IndicatorSerial.h"

class IndicatorFiles
{
  public:
    bool begin();
    bool info(meshtastic_SdCardInfo &info);
    bool command(meshtastic_SdCommand command, meshtastic_SdCardInfo &info);
    bool read(const char *path, uint32_t offset, uint8_t *buffer, uint32_t length, uint32_t &received, uint32_t &size);
    bool write(const char *path, uint32_t offset, const uint8_t *buffer, uint32_t length, bool create);
    bool remove(const char *path);
    bool list(const char *path, uint32_t offset, meshtastic_DirectoryListing &result);

  private:
    meshtastic_FileTransfer result = meshtastic_FileTransfer_init_zero;
};

extern IndicatorFiles indicatorFiles;
