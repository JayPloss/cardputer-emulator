#pragma once
// The GPIO calls the firmware makes around light sleep. Which pins wake the chip is the firmware's business;
// the emulator's light sleep wakes on a held key (GPIO11, the keyboard chip's interrupt) or the rear button
// (GPIO0) whenever either pin is enabled for wakeup.
#include <Arduino.h>  // gpio_num_t, with the pins the firmware names
typedef enum { GPIO_INTR_DISABLE = 0, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE, GPIO_INTR_ANYEDGE, GPIO_INTR_LOW_LEVEL, GPIO_INTR_HIGH_LEVEL } gpio_int_type_t;

int gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t type);
int gpio_wakeup_disable(gpio_num_t pin);
int gpio_set_intr_type(gpio_num_t pin, gpio_int_type_t type);
