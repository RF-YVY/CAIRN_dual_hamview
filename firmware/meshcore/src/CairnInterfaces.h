#pragma once
#include "CairnLocalInterface.h"
#include <helpers/MultiSerialInterface.h>

class CairnInterfaces : public MultiSerialInterface
{
  public:
    size_t writeFrame(const uint8_t *bytes, size_t length) override
    {
        if (!isEnabled())
            return 0;
        if (length && bytes[0] < 0x80 && localInterface.awaitingResponse())
            return localInterface.writeFrame(bytes, length);
        return MultiSerialInterface::writeFrame(bytes, length);
    }
};
