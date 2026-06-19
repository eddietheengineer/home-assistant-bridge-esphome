/*!
 * @file
 * @brief ESP-IDF FreeRTOS stubs for test/simulation builds.
 *
 * Provides minimal definitions of FreeRTOS types and functions so that
 * ESP-IDF-gated code compiles and exercises its logic in test builds.
 */

#ifndef ESP_FREERTOS_STUB_H
#define ESP_FREERTOS_STUB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- FreeRTOS.h types ---------- */
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

/* ---------- task.h types & functions ---------- */
typedef void* TaskHandle_t;
typedef void* StackType_t;

struct TaskControlBlock_t {
    void* dummy;
};
typedef struct TaskControlBlock_t StaticTask_t;

#define pdMS_TO_TICKS(ms) ((ms))
#define portMAX_DELAY ((UBaseType_t)-1)
#define pdTRUE ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)

static inline void vTaskDelay(UBaseType_t ticks) { (void)ticks; }
static inline void vTaskDelete(TaskHandle_t) { }
static inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 1024; }

/* ---------- queue.h types & functions ---------- */
typedef void* QueueHandle_t;

static inline QueueHandle_t xQueueCreateStatic(uint32_t count, uint32_t size,
                                                uint8_t* storage, StaticTask_t* tcb) {
    (void)count; (void)size; (void)storage; (void)tcb;
    return (QueueHandle_t)0x1;
}
static inline BaseType_t xQueueSend(QueueHandle_t q, const void* data, UBaseType_t ticks) {
    (void)q; (void)data; (void)ticks;
    return pdTRUE;
}
static inline BaseType_t xQueueReceive(QueueHandle_t q, void* data, UBaseType_t ticks) {
    (void)q; (void)data; (void)ticks;
    return pdFALSE;
}
static inline void vQueueDelete(QueueHandle_t) { }

#ifdef __cplusplus
}
#endif

#endif /* ESP_FREERTOS_STUB_H */
