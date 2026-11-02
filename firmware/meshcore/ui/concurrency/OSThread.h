#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace concurrency
{
// Adapts the shared HamView worker to MeshCore's FreeRTOS runtime.
class OSThread
{
  public:
    explicit OSThread(const char *name) : name(name), next(pending) { pending = this; }
    virtual ~OSThread()
    {
        if (task)
            vTaskDelete(task);
    }

    static bool startAll()
    {
        bool ok = true;
        for (OSThread *thread = pending; thread; thread = thread->next) {
            if (!thread->task)
                ok &= xTaskCreate(run, thread->name, 8192, thread, 1, &thread->task) == pdPASS;
        }
        return ok;
    }

  protected:
    virtual int32_t runOnce() = 0;
    void setIntervalFromNow(uint32_t)
    {
        if (task)
            xTaskNotifyGive(task);
    }

  private:
    inline static OSThread *pending = nullptr;
    const char *name;
    OSThread *next;
    TaskHandle_t task = nullptr;

    static void run(void *context)
    {
        auto *self = static_cast<OSThread *>(context);
        for (;;) {
            const int32_t interval = self->runOnce();
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval > 0 ? interval : 1));
        }
    }
};
} // namespace concurrency
