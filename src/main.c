#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	unsigned int count = 0;

	printk("Lab 2: Zephyr bring-up on %s\n", CONFIG_BOARD_TARGET);

	while (1) {
		printk("Hello World! count=%u\n", count++);
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
