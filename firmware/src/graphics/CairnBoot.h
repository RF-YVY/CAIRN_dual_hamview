#pragma once
#include <stdint.h>

namespace CairnBoot
{
constexpr uint32_t MAGIC = 0x43424E31;
enum Profile : uint8_t { Meshtastic, MeshCore };
enum Phase : uint8_t { Idle, Requested, Snapshot, Writing, Booting };
struct State {
    uint32_t magic = MAGIC;
    uint8_t active = MeshCore, target = MeshCore, previous = MeshCore, phase = Idle;
    uint8_t recovery = 0, reserved[3] = {};
};
struct Bank {
    uint32_t magic = MAGIC;
    uint16_t version = 1;
    uint8_t profile = MeshCore, generation = 0;
    uint32_t appSize = 0, filesystemSize = 0;
    uint8_t appHash[32] = {}, filesystemHash[32] = {};
};
struct Loader {
    uint32_t magic = MAGIC;
    uint32_t size = 0;
    uint8_t hash[32] = {};
};
static_assert(sizeof(State) == 12 && sizeof(Bank) == 80 && sizeof(Loader) == 40, "Boot metadata layout changed");
bool readState(State &state);
bool saveState(const State &state);
bool loadBank(uint8_t profile, Bank &bank);
bool saveBank(const Bank &bank);
bool saveLoader(const Loader &loader);
bool loaderAvailable();
bool resident();
bool residentAvailable(uint8_t profile);
bool request(uint8_t profile);
bool markHealthy(uint8_t profile);
void bankPath(char *out, unsigned capacity, const Bank &bank, bool filesystem);
} // namespace CairnBoot
