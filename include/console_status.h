/** @file console_status.h
 * @brief Fixed, in-place console status rows for periodic diagnostics.
 */
#ifndef CONSOLE_STATUS_H_
#define CONSOLE_STATUS_H_

/** @brief Set to 0 to restore plain scrolling lines for terminals without ANSI support. */
#define CONSOLE_STATUS_INPLACE 1
/** @brief Longest status line in bytes; 160 fits the current PID/ENC/CURRENT formats. */
#define CONSOLE_STATUS_LINE_MAX 160

/** @brief Pinned rows, top to bottom; event messages scroll beneath them. */
enum console_status_row {
	CONSOLE_STATUS_PID,
	CONSOLE_STATUS_ENC,
	CONSOLE_STATUS_CURRENT,
	CONSOLE_STATUS_ROW_COUNT
};

/**
 * @brief Redraw one pinned row if its text changed; never blocks the calling thread.
 * @param row Row to update.
 * @param format printk-style format for the whole row, without a trailing newline.
 */
void console_status_print(enum console_status_row row, const char *format, ...);

#endif /* CONSOLE_STATUS_H_ */
