// Checks production command serialization and response routing: asynchronous pushes must
// not complete a queued UI command or overwrite another command awaiting its response.
#include "CairnInterfaces.h"
#include "CairnLocalInterface.h"
#include <cassert>
#include <cstdio>

static unsigned calls = 0;
CairnLocalInterface localInterface;
class Phone : public BaseSerialInterface
{
  public:
    unsigned writes = 0;
    bool enabled = false;
    void enable() override { enabled = true; }
    void disable() override { enabled = false; }
    bool isEnabled() const override { return enabled; }
    bool isConnected() const override { return enabled; }
    bool isWriteBusy() const override { return false; }
    size_t checkRecvFrame(uint8_t *) override { return 0; }
    size_t writeFrame(const uint8_t *, size_t length) override
    {
        ++writes;
        return length;
    }
};
static void response(const uint8_t *bytes, size_t length, void *context)
{
    assert(length == 1 && bytes[0] == 0 && context == &calls);
    ++calls;
}

int main()
{
    CairnLocalInterface interface;
    const uint8_t command[] = {3, 0, 0, 1, 2, 3, 4, '7', '3'};
    assert(!interface.submit(command, sizeof(command), response, &calls));
    interface.enable();
    assert(!interface.submit(command, MAX_FRAME_SIZE + 1, response, &calls));
    assert(interface.submit(command, sizeof(command), response, &calls));
    assert(!interface.submit(command, sizeof(command), response, &calls));
    uint8_t frame[MAX_FRAME_SIZE];
    assert(interface.checkRecvFrame(frame) == sizeof(command));
    assert(memcmp(frame, command, sizeof(command)) == 0);
    assert(!interface.submit(command, sizeof(command), response, &calls));
    const uint8_t push[] = {0x83};
    interface.writeFrame(push, sizeof(push));
    assert(calls == 0 && !interface.submit(command, sizeof(command), response, &calls));
    const uint8_t ok[] = {0};
    interface.writeFrame(ok, sizeof(ok));
    assert(calls == 1 && interface.submit(command, sizeof(command), response, &calls));
    assert(interface.checkRecvFrame(frame) == sizeof(command));
    interface.writeFrame(ok, sizeof(ok));
    assert(calls == 2);
    CairnInterfaces router;
    Phone phone;
    assert(router.addInterface(InterfaceType::USB, &phone));
    assert(router.addInterface(InterfaceType::HardwareSerial, &localInterface));
    router.enable();
    assert(localInterface.submit(command, sizeof(command), response, &calls));
    assert(router.checkRecvFrame(frame) == sizeof(command));
    router.writeFrame(push, sizeof(push));
    assert(phone.writes == 1 && calls == 2);
    router.writeFrame(ok, sizeof(ok));
    assert(phone.writes == 1 && calls == 3);
    router.writeFrame(ok, sizeof(ok));
    assert(phone.writes == 2 && calls == 3);
    puts("Local MeshCore command serialization and asynchronous response checks passed");
}
