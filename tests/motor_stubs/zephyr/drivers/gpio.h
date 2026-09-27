#ifndef TEST_GPIO_H
#define TEST_GPIO_H
#include <stdbool.h>
#include <stdint.h>
struct gpio_dt_spec { unsigned int pin; };
#define GPIO_DT_SPEC_GET_BY_IDX(node, property, index) { .pin = (index) }
#define GPIO_OUTPUT_INACTIVE 0
bool gpio_is_ready_dt(const struct gpio_dt_spec *pin);
int gpio_pin_configure_dt(const struct gpio_dt_spec *pin, int flags);
int gpio_pin_set_dt(const struct gpio_dt_spec *pin, int value);
#endif
