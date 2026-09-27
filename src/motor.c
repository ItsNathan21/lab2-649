#include <errno.h>
#include <stdbool.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include "motor.h"

#define MOTORS DT_PATH(motors)

static const struct pwm_dt_spec enables[MOTOR_COUNT] = {
	PWM_DT_SPEC_GET_BY_IDX(MOTORS, 0),
	PWM_DT_SPEC_GET_BY_IDX(MOTORS, 1),
};
static const struct gpio_dt_spec inputs[MOTOR_COUNT][2] = {
	{ GPIO_DT_SPEC_GET_BY_IDX(MOTORS, direction_gpios, 0),
	  GPIO_DT_SPEC_GET_BY_IDX(MOTORS, direction_gpios, 1) },
	{ GPIO_DT_SPEC_GET_BY_IDX(MOTORS, direction_gpios, 2),
	  GPIO_DT_SPEC_GET_BY_IDX(MOTORS, direction_gpios, 3) },
};

/* Adjust after verifying wheel direction with a low-duty, raised-wheel test. */
static const int polarity[MOTOR_COUNT] = { 1, 1 };
K_MUTEX_DEFINE(motor_lock);
static bool ready;
static bool faulted;
static bool braking;
static int applied[MOTOR_COUNT];

static int set_enable(unsigned int side, unsigned int permille)
{
	uint32_t pulse = (uint64_t)enables[side].period * permille / 1000U;
	return pwm_set_pulse_dt(&enables[side], pulse);
}

/* Always try both, even if one fails. */
static int disable_all(void)
{
	int left = set_enable(MOTOR_LEFT, 0);
	int right = set_enable(MOTOR_RIGHT, 0);
	/* STM32 compare registers are preloaded: zero takes effect at the
	 * next timer update, not necessarily when pwm_set returns.
	 */
	k_busy_wait((MAX(enables[0].period, enables[1].period) + 999U) / 1000U + 1U);
	applied[0] = applied[1] = 0;
	return left != 0 ? left : right;
}

static int fail(int error)
{
	faulted = true;
	(void)disable_all();
	return error;
}

static int available(void)
{
	return !ready ? -ENODEV : faulted ? -EIO : 0;
}

int motors_init(void)
{
	int ret = 0;
	k_mutex_lock(&motor_lock, K_FOREVER);
	if (ready || faulted) {
		ret = -EALREADY;
		goto out;
	}
	/* Check every resource before changing pins. */
	for (unsigned int side = 0; side < MOTOR_COUNT; side++) {
		if (!pwm_is_ready_dt(&enables[side])) {
			ret = -ENODEV;
			goto out;
		}
		for (unsigned int pin = 0; pin < 2; pin++) {
			if (!gpio_is_ready_dt(&inputs[side][pin])) {
				ret = -ENODEV;
				goto out;
			}
		}
	}
	ret = disable_all();
	if (ret != 0) {
		ret = fail(ret);
		goto out;
	}
	for (unsigned int side = 0; side < MOTOR_COUNT; side++) {
		for (unsigned int pin = 0; pin < 2; pin++) {
			ret = gpio_pin_configure_dt(&inputs[side][pin], GPIO_OUTPUT_INACTIVE);
			if (ret != 0) {
				ret = fail(ret);
				goto out;
			}
		}
	}
	ready = true;
out:
	k_mutex_unlock(&motor_lock);
	return ret;
}

int motor_drive(enum motor_side side, int duty_permille)
{
	int ret;
	if ((unsigned int)side >= MOTOR_COUNT || duty_permille < -1000 ||
	    duty_permille > 1000) {
		return -EINVAL;
	}
	k_mutex_lock(&motor_lock, K_FOREVER);
	ret = available();
	if (ret != 0) {
		goto out;
	}
	if (braking) {
		ret = -EPERM;
		goto out;
	}
	int duty = duty_permille * polarity[side];
	if ((applied[side] > 0 && duty < 0) || (applied[side] < 0 && duty > 0)) {
		ret = -EBUSY;
		goto out;
	}
	if (duty == 0) {
		ret = set_enable(side, 0);
	} else {
		/* For a start, set direction with the bridge disabled. Same-direction
		 * speed updates only change PWM, avoiding an unnecessary output gap.
		 */
		if (applied[side] == 0) {
			ret = set_enable(side, 0);
			k_busy_wait((enables[side].period + 999U) / 1000U + 1U);
			if (ret == 0) {
				ret = gpio_pin_set_dt(&inputs[side][0], duty > 0);
			}
			if (ret == 0) {
				ret = gpio_pin_set_dt(&inputs[side][1], duty < 0);
			}
			if (ret != 0) {
				ret = fail(ret);
				goto out;
			}
		}
		ret = set_enable(side, duty > 0 ? duty : -duty);
	}
	if (ret != 0) {
		ret = fail(ret);
	} else {
		applied[side] = duty;
	}
out:
	k_mutex_unlock(&motor_lock);
	return ret;
}

int motors_brake(void)
{
	k_mutex_lock(&motor_lock, K_FOREVER);
	int ret = available();
	if (ret != 0) {
		goto out;
	}
	braking = true;
	ret = disable_all();
	if (ret != 0) {
		ret = fail(ret);
		goto out;
	}
	for (unsigned int side = 0; side < MOTOR_COUNT; side++) {
		ret = gpio_pin_set_dt(&inputs[side][0], 0);
		if (ret == 0) {
			ret = gpio_pin_set_dt(&inputs[side][1], 0);
		}
		if (ret == 0) {
			/* Static HIGH enable + IN1=IN2=LOW = dynamic braking.
			 * A zero-duty enable would COAST instead (L298 truth table).
			 */
			ret = set_enable(side, 1000);
		}
		if (ret != 0) {
			ret = fail(ret);
			goto out;
		}
	}
out:
	k_mutex_unlock(&motor_lock);
	return ret;
}

static int stop(bool release_brake)
{
	k_mutex_lock(&motor_lock, K_FOREVER);
	int ret = available();
	if (ret == 0 && braking && !release_brake) {
		ret = -EPERM;
	}
	if (ret == 0) {
		ret = disable_all();
		if (ret != 0) {
			ret = fail(ret);
		} else {
			braking = false;
		}
	}
	k_mutex_unlock(&motor_lock);
	return ret;
}

int motors_coast(void)
{
	return stop(false);
}

int motors_release_brake(void)
{
	return stop(true);
}
