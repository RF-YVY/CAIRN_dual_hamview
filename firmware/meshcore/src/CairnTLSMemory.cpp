#include <esp_heap_caps.h>
#include <stddef.h>
#include <stdint.h>

extern "C" void *__wrap_esp_mbedtls_mem_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
        return nullptr;
    return heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
