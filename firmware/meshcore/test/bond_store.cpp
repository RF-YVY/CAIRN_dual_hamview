// Pins the Bluetooth namespace boundary so MeshCore cannot replace Meshtastic
// bonds, while preserving ordinary NVS access modes, handles and errors.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <nvs.h>

extern "C" esp_err_t __wrap_nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *);
static const char *opened;
static nvs_open_mode_t access;
static int result = 0;
extern "C" esp_err_t __real_nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    opened = name;
    access = mode;
    *handle = 42;
    return result;
}
int main()
{
    nvs_handle_t handle;
    assert(__wrap_nvs_open("nimble_bond", NVS_READWRITE, &handle) == 0);
    assert(strcmp(opened, "cairn_mc_bond") == 0 && handle == 42 && access == NVS_READWRITE);
    for (auto *name : {"cairn_boot", "cairn_ui", "cairn_link", "nimble_bonds", ""}) {
        result = -7;
        assert(__wrap_nvs_open(name, NVS_READONLY, &handle) == -7);
        assert(opened == name && access == NVS_READONLY);
    }
    __wrap_nvs_open(nullptr, NVS_READONLY, &handle);
    assert(opened == nullptr);
    puts("MeshCore bond isolation and unchanged application NVS checks passed");
}
