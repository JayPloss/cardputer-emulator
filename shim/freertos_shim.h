#pragma once
// FreeRTOS as the firmware uses it: tasks are host threads, mutexes are host mutexes.
#include <stddef.h>
#include <stdint.h>
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef void (*TaskFunction_t)(void *);
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xFFFFFFFFu
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_DMA 4
#define MALLOC_CAP_SPIRAM 8
#ifdef __cplusplus
extern "C" {
#endif
void vTaskDelay(TickType_t ticks);
size_t heap_caps_get_allocated_size(void *block);
struct portMUX_TYPE { volatile int locked; };
#define portMUX_INITIALIZER_UNLOCKED {0}
void portENTER_CRITICAL(struct portMUX_TYPE *mux);
void portEXIT_CRITICAL(struct portMUX_TYPE *mux);
int xPortInIsrContext(void);
void vTaskDelete(TaskHandle_t task);
void *xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, unsigned priority, TaskHandle_t *handle, int core);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex);
void *heap_caps_malloc(size_t bytes, uint32_t caps);
#ifdef __cplusplus
}
#endif
