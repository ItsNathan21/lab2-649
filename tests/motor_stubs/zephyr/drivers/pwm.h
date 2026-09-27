#ifndef TEST_PWM_H
#define TEST_PWM_H
#include <stdbool.h>
#include <stdint.h>
struct pwm_dt_spec { unsigned int channel; uint32_t period; };
#define PWM_DT_SPEC_GET_BY_IDX(node, index) { .channel = (index), .period = 100000 }
bool pwm_is_ready_dt(const struct pwm_dt_spec *pwm);
int pwm_set_pulse_dt(const struct pwm_dt_spec *pwm, uint32_t pulse);
#endif
