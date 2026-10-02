/** @file motor.c
 * @brief Serialized H-bridge output control and fault handling.
 */
#include <errno.h>
#include <stdbool.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

#include "motor.h"
#include "test_points.h"

/* Overlay indices follow bridge A/B; physical left is B and physical right is A. */
static const struct pwm_dt_spec enables[MOTOR_COUNT] = {
	[MOTOR_LEFT] = PWM_DT_SPEC_GET_BY_IDX(DT_PATH(motors), 1),
	[MOTOR_RIGHT] = PWM_DT_SPEC_GET_BY_IDX(DT_PATH(motors), 0),
};
static const struct gpio_dt_spec inputs[MOTOR_COUNT][2] = {
	[MOTOR_LEFT] = {
		GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(motors), direction_gpios, 2),
		GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(motors), direction_gpios, 3),
	},
	[MOTOR_RIGHT] = {
		GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(motors), direction_gpios, 0),
		GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(motors), direction_gpios, 1),
	},
};

/* Preserve forward rotation: bridge B uses reversed inputs, bridge A uses normal inputs. */
static const int polarity[MOTOR_COUNT] = {
	[MOTOR_LEFT] = -1,
	[MOTOR_RIGHT] = 1,
};
static K_MUTEX_DEFINE(motor_lock);
static bool ready;
static bool faulted;
static bool braking;
static int applied[MOTOR_COUNT];

/**
 * @brief Round a motor duty to the nearest PWM timer tick.
 * @param side Motor index.
 * @param duty Unsigned duty from 0 to MOTOR_DUTY_FULL_SCALE.
 * @return 0 on success, or a negative PWM/range error.
 */
static int set_enable(unsigned int side, uint32_t duty)
{
	const struct pwm_dt_spec *pwm = &enables[side];
	uint64_t rate;
	int ret = pwm_get_cycles_per_sec(pwm->dev, pwm->channel, &rate);

	if (ret != 0) {
		return ret;
	}
	uint64_t period = rate * pwm->period / NSEC_PER_SEC;

	if (period == 0U || period > UINT32_MAX) {
		return -ERANGE;
	}
	/* Round directly to the nearest timer tick, without a permille step. */
	uint32_t pulse = (period * duty + MOTOR_DUTY_FULL_SCALE / 2U) /
			 MOTOR_DUTY_FULL_SCALE;

	ret = pwm_set_cycles(pwm->dev, pwm->channel, (uint32_t)period, pulse, pwm->flags);
	if (ret == 0 && side == MOTOR_RIGHT) {
		test_points_pwm_written();
	}
	return ret;
}

/* Always try both, even if one fails. */
/**
 * @brief Disable both enables and wait for timer preloads to settle.
 * @return 0 on success, or the first PWM error.
 */
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

/**
 * @brief Latch a hardware fault and attempt to disable both motors.
 * @param error Original negative driver error.
 * @return The original error.
 */
static int fail(int error)
{
	faulted = true;
	(void)disable_all();
	return error;
}

/**
 * @brief Check whether the motor driver can accept commands.
 * @return 0 if ready, -ENODEV before initialization, or -EIO after a fault.
 */
static int available(void)
{
	return !ready ? -ENODEV : faulted ? -EIO : 0;
}

/**
 * @brief Initialize both motor outputs in coast; call from thread context.
 * @return 0 on success, or a negative initialization error.
 */
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

/**
 * @brief Set signed duty, rounded to the nearest available PWM tick.
 * @param side MOTOR_LEFT or MOTOR_RIGHT.
 * @param duty_raw Signed duty in -65535..65535; zero coasts.
 * @return 0 on success, or a negative error; see motor.h for interlocks.
 */
int motor_drive_raw(enum motor_side side, int32_t duty_raw)
{
	int ret;
	if ((unsigned int)side >= MOTOR_COUNT || duty_raw < -(int32_t)MOTOR_DUTY_FULL_SCALE ||
	    duty_raw > (int32_t)MOTOR_DUTY_FULL_SCALE) {
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
	int32_t duty = duty_raw * polarity[side];
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

/**
 * @brief Set motor duty in signed thousandths from thread context.
 * @param side MOTOR_LEFT or MOTOR_RIGHT.
 * @param duty_permille Duty in -1000..1000; zero coasts.
 * @return 0 on success, or a negative error; see motor.h for interlocks.
 */
int motor_drive(enum motor_side side, int duty_permille)
{
	if (duty_permille < -1000 || duty_permille > 1000) {
		return -EINVAL;
	}
	int32_t scaled = duty_permille * (int32_t)MOTOR_DUTY_FULL_SCALE;

	scaled = (scaled + (scaled < 0 ? -500 : 500)) / 1000;
	return motor_drive_raw(side, scaled);
}

/**
 * @brief Apply equal-input braking on both bridges with proportional enable PWM.
 * @param duty_raw Brake duty in 1..65535; release through motors_release_brake().
 * @return 0 on success, or a negative range/driver error.
 */
int motors_brake_raw(uint32_t duty_raw)
{
	if (duty_raw == 0U || duty_raw > MOTOR_DUTY_FULL_SCALE) {
		return -EINVAL;
	}
	k_mutex_lock(&motor_lock, K_FOREVER);
	int ret = available();

	if (ret != 0) {
		goto out;
	}
	if (!braking) {
		/* Disable and settle PWM before changing from drive to equal inputs. */
		ret = disable_all();
		for (unsigned int side = 0; side < MOTOR_COUNT && ret == 0; side++) {
			ret = gpio_pin_set_dt(&inputs[side][0], 0);
			if (ret == 0) {
				ret = gpio_pin_set_dt(&inputs[side][1], 0);
			}
		}
		if (ret != 0) {
			ret = fail(ret);
			goto out;
		}
		braking = true;
	}
	/* Inputs stay LOW: enable HIGH brakes, enable LOW coasts (L298 truth table). */
	for (unsigned int side = 0; side < MOTOR_COUNT; side++) {
		ret = set_enable(side, duty_raw);
		if (ret != 0) {
			ret = fail(ret);
			goto out;
		}
	}
out:
	k_mutex_unlock(&motor_lock);
	return ret;
}

/**
 * @brief Latch full electrical braking on both wheels until explicitly released.
 * @return 0 on success, or a negative motor error.
 */
int motors_brake(void)
{
	return motors_brake_raw(MOTOR_DUTY_FULL_SCALE);
}

/**
 * @brief Coast both motors while respecting the latched brake.
 * @param release_brake True to clear braking; false to preserve the latch.
 * @return 0 on success, -EPERM if braking is preserved, or a driver error.
 */
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

/**
 * @brief Disable both enables without overriding a latched brake.
 * @return 0 on success, -EPERM while braking, or a driver error.
 */
int motors_coast(void)
{
	return stop(false);
}

/**
 * @brief Clear the brake latch into coast without resuming old duty.
 * @return 0 on success, or a negative motor error.
 */
int motors_release_brake(void)
{
	return stop(true);
}
