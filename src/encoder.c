#include <errno.h>
#include <stdbool.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "encoder.h"
#include "quadrature.h"

#define ENCODERS DT_PATH(encoders)

/* A/B pairs must all use one GPIO port, for one simultaneous input read. */
static const struct gpio_dt_spec channels[ENCODER_COUNT][2] = {
	{ GPIO_DT_SPEC_GET_BY_IDX(ENCODERS, left_gpios, 0),
	  GPIO_DT_SPEC_GET_BY_IDX(ENCODERS, left_gpios, 1) },
	{ GPIO_DT_SPEC_GET_BY_IDX(ENCODERS, right_gpios, 0),
	  GPIO_DT_SPEC_GET_BY_IDX(ENCODERS, right_gpios, 1) },
};

/* Set a side to -1 if its count decreases when its wheel turns forward. */
static const int direction[ENCODER_COUNT] = { 1, 1 };
static struct gpio_callback callback;
static struct encoder_snapshot state;
static uint8_t previous[ENCODER_COUNT];
static bool have_previous;
static bool initialized;

static uint8_t ab_state(gpio_port_value_t value, unsigned int side)
{
	return (!!(value & BIT(channels[side][0].pin)) << 1) |
	       !!(value & BIT(channels[side][1].pin));
}

static void on_edge(const struct device *port, struct gpio_callback *cb,
		    gpio_port_pins_t pins)
{
	gpio_port_value_t value;
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* All four channels sampled together. No logging or velocity math here. */
	if (gpio_port_get_raw(port, &value) != 0) {
		state.read_errors++;
		have_previous = false;
		return;
	}
	for (unsigned int side = 0; side < ENCODER_COUNT; side++) {
		uint8_t current = ab_state(value, side);
		if (have_previous) {
			if ((previous[side] ^ current) == 3) {
				state.invalid_transitions[side]++;
			} else {
				state.counts[side] += direction[side] *
					quadrature_step(previous[side], current);
			}
		}
		previous[side] = current;
	}
	have_previous = true;
}

int encoders_init(void)
{
	const struct device *port = channels[0][0].port;
	gpio_port_pins_t mask = 0;
	gpio_port_value_t value;
	int ret;

	if (initialized) {
		return -EALREADY;
	}
	if (!device_is_ready(port)) {
		return -ENODEV;
	}
	for (unsigned int side = 0; side < ENCODER_COUNT; side++) {
		for (unsigned int phase = 0; phase < 2; phase++) {
			const struct gpio_dt_spec *pin = &channels[side][phase];
			if (pin->port != port || (mask & BIT(pin->pin))) {
				return -EINVAL;
			}
			mask |= BIT(pin->pin);
			ret = gpio_pin_configure_dt(pin, GPIO_INPUT);
			if (ret != 0) {
				return ret;
			}
		}
	}
	ret = gpio_port_get_raw(port, &value);
	if (ret != 0) {
		return ret;
	}
	state = (struct encoder_snapshot){0};
	for (unsigned int side = 0; side < ENCODER_COUNT; side++) {
		previous[side] = ab_state(value, side);
	}
	have_previous = true;
	gpio_init_callback(&callback, on_edge, mask);
	ret = gpio_add_callback(port, &callback);
	if (ret != 0) {
		return ret;
	}
	for (unsigned int side = 0; side < ENCODER_COUNT; side++) {
		for (unsigned int phase = 0; phase < 2; phase++) {
			ret = gpio_pin_interrupt_configure_dt(&channels[side][phase],
							      GPIO_INT_EDGE_BOTH);
			if (ret != 0) {
				goto cleanup;
			}
		}
	}
	initialized = true;
	return 0;

cleanup:
	for (unsigned int side = 0; side < ENCODER_COUNT; side++) {
		for (unsigned int phase = 0; phase < 2; phase++) {
			(void)gpio_pin_interrupt_configure_dt(&channels[side][phase],
							     GPIO_INT_DISABLE);
		}
	}
	(void)gpio_remove_callback(port, &callback);
	return ret;
}

void encoders_snapshot(struct encoder_snapshot *out)
{
	/* F401RE is single-core. Protect 64-bit counts against ISR updates. */
	unsigned int key = irq_lock();
	*out = state;
	out->timestamp_ms = k_uptime_get();
	irq_unlock(key);
}
