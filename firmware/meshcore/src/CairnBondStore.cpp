#include <nvs.h>
#include <string.h>

extern "C" esp_err_t __real_nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *);

extern "C" esp_err_t __wrap_nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    // Keep MeshCore's bonds separate from Meshtastic's NimBLE store.
    return __real_nvs_open(name && strcmp(name, "nimble_bond") == 0 ? "cairn_mc_bond" : name, mode, handle);
}
