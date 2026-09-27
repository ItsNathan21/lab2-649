/* Test the real driver with fake GPIO/PWM. Emulate delayed timer preloads to
 * catch direction changes made before the enable actually goes LOW.
 * Mutex stubs do not test thread scheduling or real hardware timing.
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include "motor.h"

static uint32_t pending[2], actual[2];
static int pins[4];
static int pwm_failure, gpio_failure;
static unsigned int writes;

bool gpio_is_ready_dt(const struct gpio_dt_spec *pin) { (void)pin; return true; }
bool pwm_is_ready_dt(const struct pwm_dt_spec *pwm) { (void)pwm; return true; }

void k_busy_wait(unsigned int usec)
{
	assert(usec >= 100);
	actual[0] = pending[0];
	actual[1] = pending[1];
}

int pwm_set_pulse_dt(const struct pwm_dt_spec *pwm, uint32_t pulse)
{
	writes++;
	assert(pulse <= pwm->period);
	if (pwm_failure) { pwm_failure = 0; return -EIO; }
	pending[pwm->channel] = pulse;
	return 0;
}

int gpio_pin_set_dt(const struct gpio_dt_spec *pin, int value)
{
	writes++;
	assert(actual[pin->pin / 2] == 0); /* Never switch direction under PWM. */
	if (gpio_failure) { gpio_failure = 0; return -EIO; }
	pins[pin->pin] = value;
	return 0;
}

int gpio_pin_configure_dt(const struct gpio_dt_spec *pin, int flags)
{
	(void)flags;
	return gpio_pin_set_dt(pin, 0);
}

int main(int argc, char **argv)
{
	assert(motor_drive(MOTOR_LEFT, 200) == -ENODEV);
	if (argc > 1 && strcmp(argv[1], "init-fault") == 0) {
		gpio_failure = 1;
		assert(motors_init() == -EIO);
		assert(actual[0] == 0 && actual[1] == 0);
		assert(motors_init() == -EALREADY);
		puts("motor initialization fault test passed");
		return 0;
	}
	assert(motors_init() == 0);
	assert(motors_init() == -EALREADY);
	assert(actual[0] == 0 && actual[1] == 0);
	assert(motor_drive(MOTOR_LEFT, 200) == 0);
	k_busy_wait(100);
	assert(actual[0] == 20000 && actual[1] == 0);
	assert(pins[0] == 1 && pins[1] == 0);
	assert(motor_drive(MOTOR_RIGHT, -300) == 0);
	k_busy_wait(100);
	assert(actual[1] == 30000 && pins[2] == 0 && pins[3] == 1);
	unsigned int before = writes;
	assert(motor_drive(MOTOR_LEFT, 1001) == -EINVAL);
	assert(motor_drive((enum motor_side)-1, 100) == -EINVAL);
	assert(motor_drive(MOTOR_LEFT, -200) == -EBUSY);
	assert(writes == before);
	assert(motor_drive(MOTOR_LEFT, 1000) == 0);
	k_busy_wait(100);
	assert(actual[0] == 100000);
	assert(motors_brake() == 0);
	k_busy_wait(100);
	assert(actual[0] == 100000 && actual[1] == 100000);
	assert(pins[0] == 0 && pins[1] == 0 && pins[2] == 0 && pins[3] == 0);
	before = writes;
	assert(motor_drive(MOTOR_LEFT, 500) == -EPERM);
	assert(motor_drive(MOTOR_RIGHT, 0) == -EPERM);
	assert(motors_coast() == -EPERM);
	assert(writes == before);
	assert(motors_release_brake() == 0);
	assert(actual[0] == 0 && actual[1] == 0);
	assert(motor_drive(MOTOR_LEFT, -250) == 0);
	k_busy_wait(100);
	assert(pins[0] == 0 && pins[1] == 1 && actual[0] == 25000);
	assert(motor_drive(MOTOR_LEFT, 0) == 0);
	/* No external wait: driver must allow timer preload to settle itself. */
	assert(motor_drive(MOTOR_LEFT, 100) == 0);
	assert(motors_coast() == 0);
	assert(actual[0] == 0 && actual[1] == 0);
	assert(motor_drive(MOTOR_RIGHT, 400) == 0);
	k_busy_wait(100);
	assert(actual[1] == 40000);
	if (argc > 1 && strcmp(argv[1], "gpio-fault") == 0) {
		gpio_failure = 1;
	} else {
		pwm_failure = 1;
	}
	if (argc > 1 && strcmp(argv[1], "brake-fault") == 0) {
		assert(motors_brake() == -EIO);
	} else {
		assert(motor_drive(MOTOR_LEFT, 200) == -EIO);
	}
	assert(actual[0] == 0 && actual[1] == 0);
	assert(motor_drive(MOTOR_RIGHT, 200) == -EIO);
	assert(motors_release_brake() == -EIO);
	puts("motor control and fault tests passed");
	return 0;
}
