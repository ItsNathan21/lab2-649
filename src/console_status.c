/** @file console_status.c
 * @brief ANSI cursor control that pins diagnostic rows above a scrolling message area.
 */
#include <stdarg.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "console_status.h"

static K_MUTEX_DEFINE(status_lock);
static char last_text[CONSOLE_STATUS_ROW_COUNT][CONSOLE_STATUS_LINE_MAX];

/**
 * @brief Redraw one pinned row if its text changed; skips the update if the console is busy.
 * @param row Row to update.
 * @param format printk-style format for the whole row, without a trailing newline.
 */
void console_status_print(enum console_status_row row, const char *format, ...)
{
	char text[CONSOLE_STATUS_LINE_MAX];
	va_list args;

	if ((unsigned int)row >= CONSOLE_STATUS_ROW_COUNT) {
		return;
	}
	va_start(args, format);
	vsnprintk(text, sizeof(text), format, args);
	va_end(args);
	if (!CONSOLE_STATUS_INPLACE) {
		printk("%s\n", text);
		return;
	}
	/* Control threads call this; skip a redraw rather than wait for another printer. */
	if (k_mutex_lock(&status_lock, K_NO_WAIT) != 0) {
		return;
	}
	if (strcmp(text, last_text[row]) != 0) {
		/* Save cursor, keep rows above the scroll region, draw the row without wrapping
		 * into the next row, then restore cursor. Re-sending the region each time repairs
		 * a terminal attached after boot.
		 */
		printk("\0337\033[%u;r\033[%u;1H\033[2K\033[?7l%s\033[?7h\0338",
		       CONSOLE_STATUS_ROW_COUNT + 2U, (unsigned int)row + 1U, text);
		strcpy(last_text[row], text);
	}
	k_mutex_unlock(&status_lock);
}
