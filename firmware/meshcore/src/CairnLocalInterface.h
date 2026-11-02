#pragma once

#include <cstring>
#include <helpers/BaseSerialInterface.h>

class CairnLocalInterface : public BaseSerialInterface
{
  public:
    using Response = void (*)(const uint8_t *, size_t, void *);
    using Observer = void (*)(const uint8_t *, size_t);
    void setObserver(Observer observer) { observe = observer; }
    bool awaitingResponse() const { return awaiting; }
    bool submit(const uint8_t *bytes, size_t length, Response response, void *context)
    {
        if (!enabled || queued || awaiting || !length || length > MAX_FRAME_SIZE)
            return false;
        memcpy(frame, bytes, length);
        queued = length;
        callback = response;
        userdata = context;
        return true;
    }
    void enable() override { enabled = true; }
    void disable() override { enabled = false; }
    bool isEnabled() const override { return enabled; }
    bool isConnected() const override { return enabled; }
    bool isWriteBusy() const override { return false; }
    size_t checkRecvFrame(uint8_t *out) override
    {
        if (!enabled || !queued)
            return 0;
        const size_t length = queued;
        memcpy(out, frame, length);
        queued = 0;
        awaiting = true;
        return length;
    }
    size_t writeFrame(const uint8_t *bytes, size_t length) override
    {
        if (observe)
            observe(bytes, length);
        if (awaiting && length && bytes[0] < 0x80) {
            awaiting = false;
            if (callback)
                callback(bytes, length, userdata);
        }
        return length;
    }

  private:
    bool enabled = false, awaiting = false;
    size_t queued = 0;
    uint8_t frame[MAX_FRAME_SIZE];
    Response callback = nullptr;
    void *userdata = nullptr;
    Observer observe = nullptr;
};

extern CairnLocalInterface localInterface;
