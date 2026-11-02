#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace concurrency
{
class Lock
{
  public:
    Lock() : handle(xSemaphoreCreateMutex()) { configASSERT(handle); }
    ~Lock() { vSemaphoreDelete(handle); }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
    void lock()
    {
        if (xSemaphoreTake(handle, portMAX_DELAY) != pdTRUE)
            abort();
    }
    bool lock(uint32_t milliseconds) { return xSemaphoreTake(handle, pdMS_TO_TICKS(milliseconds)) == pdTRUE; }
    void unlock()
    {
        if (xSemaphoreGive(handle) != pdTRUE)
            abort();
    }

  private:
    SemaphoreHandle_t handle;
};
} // namespace concurrency
