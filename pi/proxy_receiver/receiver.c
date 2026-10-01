/** @file receiver.c
 * @brief Pi UDP-to-UART forwarding and packet diagnostics.
 */
#include "receiver.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include "uart_protocol.h"

static volatile sig_atomic_t keep_running = 1;

/** @brief Format one current value, explicitly marking stale or uncalibrated data.
 * @param output Destination of at least 24 bytes.
 * @param currents Latest decoded status currents.
 * @param channel Left, right, or servo index.
 * @param fresh True only while the STM heartbeat remains live.
 */
static void format_current(char *output, const struct uart_current_status *currents,
			   unsigned int channel, bool fresh)
{
	if (fresh && (currents->valid_mask & (1U << channel)) != 0U) {
		snprintf(output, 24, "%d mA", (int)currents->milliamps[channel]);
	} else {
		snprintf(output, 24, "unavailable");
	}
}

/** @brief Request loop exit so diagnostic descriptor flags can be restored.
 * @param signal_number Delivered termination signal.
 */
static void stop_receiver(int signal_number)
{
	(void)signal_number;
	keep_running = 0;
}

/* UDP remains the course counter + DIJOYSTATE2 format.
 * UART uses the shared framed command/status protocol in uart_protocol.h.
 */

/**
 * @brief Decode an unsigned little-endian 32-bit value.
 * @param p Buffer containing at least four readable bytes.
 * @return Decoded value.
 */
static uint32_t get_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief Decode a signed little-endian 32-bit value.
 * @param p Buffer containing at least four readable bytes.
 * @return Decoded value.
 */
static int32_t get_i32(const uint8_t *p)
{
	uint32_t v = get_u32(p);
	return (int32_t)((v & UINT32_C(0x80000000)) ?
					 (int64_t)v - INT64_C(4294967296) : (int64_t)v);
}

/**
 * @brief Encode a 16-bit value in little-endian order.
 * @param p Destination with at least two writable bytes.
 * @param v Value to encode.
 */
static void put_u16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

/**
 * @brief Open a UART at 115200 baud, 8N1, without flow control.
 * @param path UART device pathname.
 * @return File descriptor on success, or -1 on failure.
 */
static int open_uart(const char *path)
{
	int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		perror("open UART");
		return -1;
	}
	struct termios t;
	if (tcgetattr(fd, &t) < 0) {
		perror("tcgetattr");
		close(fd);
		return -1;
	}
	cfmakeraw(&t);
	t.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
	t.c_cflag |= CS8 | CLOCAL | CREAD;
	t.c_iflag &= ~(IXON | IXOFF | IXANY);
	if (cfsetispeed(&t, B115200) < 0 || cfsetospeed(&t, B115200) < 0 ||
		tcsetattr(fd, TCSANOW, &t) < 0) {
		perror("configure UART");
		close(fd);
		return -1;
	}
	return fd;
}


/** @brief Read monotonic milliseconds for freshness and absolute transmission scheduling.
 * @return Milliseconds since an unspecified monotonic origin.
 */
static int64_t now_ms(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/** @brief Send one short frame without waiting indefinitely for the UART.
 * @param fd UART descriptor.
 * @param frame Frame fields.
 * @return 0 on success, or -1 on an I/O error or bounded write timeout.
 */
static int send_frame(int fd, const struct uart_frame *frame)
{
	uint8_t bytes[UART_FRAME_SIZE];
	size_t sent = 0;
	int64_t deadline = now_ms() + PI_UART_WRITE_TIMEOUT_MS;

	uart_frame_encode(frame, bytes);
	while (sent < sizeof(bytes)) {
		ssize_t count = write(fd, bytes + sent, sizeof(bytes) - sent);

		if (count > 0) {
			sent += (size_t)count;
			continue;
		}
		if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
			return -1;
		}
		int64_t remaining = deadline - now_ms();

		if (remaining <= 0) {
			errno = ETIMEDOUT;
			return -1;
		}
		struct pollfd output = {.fd = fd, .events = POLLOUT};
		int ret = poll(&output, 1, (int)remaining);

		if (ret < 0 && errno != EINTR) {
			return -1;
		}
	}
	return 0;
}

/** @brief Decode and print one UDP wheel datagram without accepting invalid axes.
 * @param bytes UDP datagram bytes.
 * @param length Actual datagram length.
 * @param command Destination wheel payload.
 * @return True for a correctly sized, range-valid datagram.
 */
static bool decode_wheel(const uint8_t *bytes, ssize_t length, struct uart_frame *command)
{
	if (length != UDP_SIZE) {
		printf("UDP bytes=%zd: expected %d; skipped\n", length, UDP_SIZE);
		return false;
	}
	int32_t steering = get_i32(bytes + 4);
	int32_t throttle = get_i32(bytes + 8);
	int32_t brake = get_i32(bytes + 24);
	uint16_t buttons = 0;
	static const unsigned int indices[] = {0, 1, 2, 3, 4, 5, 8, 9};

	for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); i++) {
		if ((bytes[52 + indices[i]] & 0x80U) != 0U) {
			buttons |= (uint16_t)(1U << indices[i]);
		}
	}
	printf("UDP seq=%" PRIu32 " steering=%" PRId32 " throttle=%" PRId32
	       " brake=%" PRId32 " buttons=0x%04x\n",
	       get_u32(bytes), steering, throttle, brake, (unsigned int)buttons);
	if (steering < INT16_MIN || steering > INT16_MAX ||
	    throttle < INT16_MIN || throttle > INT16_MAX ||
	    brake < INT16_MIN || brake > INT16_MAX) {
		return false;
	}
	put_u16(command->payload, (uint16_t)steering);
	put_u16(command->payload + 2, (uint16_t)throttle);
	put_u16(command->payload + 4, (uint16_t)brake);
	put_u16(command->payload + 6, buttons);
	return true;
}

/** @brief Exchange periodic UART frames while accepting fresh wheel UDP input.
 * @param argc Must be two.
 * @param argv Program name and UART device path.
 * @return EXIT_SUCCESS on termination, or EXIT_FAILURE on arguments/I/O failure.
 */
int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc != 2) {
		fprintf(stderr, "Usage: %s /dev/serial0\n", argv[0]);
		return EXIT_FAILURE;
	}
	int uart = open_uart(argv[1]);

	if (uart < 0) {
		return EXIT_FAILURE;
	}
	int sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
	struct sockaddr_in local = {
		.sin_family = AF_INET,
		.sin_port = htons(UDP_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	if (sock < 0 || bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
		perror("UDP socket/bind");
		close(uart);
		if (sock >= 0) {
			close(sock);
		}
		return EXIT_FAILURE;
	}
	char *resolved = realpath(argv[1], NULL);

	printf("UDP :%d <-> UART %s (%s), 115200 8N1, framed 20 ms heartbeat\n",
	       UDP_PORT, argv[1], resolved != NULL ? resolved : "unresolved");
	free(resolved);
	/* Diagnostics must not stall safety traffic when an SSH terminal stops draining. */
	struct sigaction action = {.sa_handler = stop_receiver};

	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0) {
		perror("signal handlers");
		close(sock);
		close(uart);
		return EXIT_FAILURE;
	}
	int stdout_flags = fcntl(STDOUT_FILENO, F_GETFL);

	if (stdout_flags < 0 || fcntl(STDOUT_FILENO, F_SETFL, stdout_flags | O_NONBLOCK) < 0) {
		perror("nonblocking diagnostics");
		close(sock);
		close(uart);
		return EXIT_FAILURE;
	}
	struct uart_frame command = {.type = UART_FRAME_COMMAND};
	struct uart_parser parser = {0};
	struct pollfd inputs[] = {
		{.fd = sock, .events = POLLIN},
		{.fd = uart, .events = POLLIN},
	};
	int64_t last_wheel = now_ms() - PI_WHEEL_TIMEOUT_MS;
	int64_t last_status = now_ms() - UART_LINK_TIMEOUT_MS;
	int64_t next_tx = now_ms();
	int64_t last_print = 0;
	uint32_t wheel_sequence = 0;
	uint16_t status_sequence = 0;
	uint8_t status_faults = UART_FAULT_LINK;
	struct uart_current_status currents = {0};
	bool have_wheel = false;
	bool have_status = false;

	while (keep_running != 0) {
		clearerr(stdout);
		int64_t now = now_ms();

		if (now >= next_tx) {
			command.flags = 0;
			if (have_wheel && now - last_wheel < PI_WHEEL_TIMEOUT_MS) {
				command.flags |= UART_COMMAND_WHEEL_FRESH;
			}
			if (have_status && now - last_status < UART_LINK_TIMEOUT_MS) {
				command.flags |= UART_COMMAND_STATUS_FRESH;
			}
			if (send_frame(uart, &command) != 0) {
				perror("UART write");
				break;
			}
			command.sequence++;
			next_tx += UART_LINK_PERIOD_MS;
			if (next_tx <= now_ms()) {
				next_tx = now_ms() + UART_LINK_PERIOD_MS;
			}
		}
		if (now - last_print >= PI_STATUS_PRINT_MS) {
			char current_text[UART_CURRENT_COUNT][24];

			for (unsigned int i = 0; i < UART_CURRENT_COUNT; i++) {
				format_current(current_text[i], &currents, i,
					       now - last_status < UART_LINK_TIMEOUT_MS);
			}
			printf("STM heartbeat=%s faults=0x%02x wheel=%s current L=%s R=%s S=%s\n",
			       now - last_status < UART_LINK_TIMEOUT_MS ? "OK" : "MISSING",
			       (unsigned int)status_faults,
			       now - last_wheel < PI_WHEEL_TIMEOUT_MS ? "fresh" : "STALE",
			       current_text[0], current_text[1], current_text[2]);
			last_print = now;
		}
		int64_t wait = next_tx - now_ms();
		int ret = poll(inputs, 2, wait > 0 ? (int)wait : 0);

		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("poll");
			break;
		}
		if (((inputs[0].revents | inputs[1].revents) &
		     (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			fprintf(stderr, "UART/UDP descriptor lost; stopping\n");
			break;
		}
		if ((inputs[1].revents & POLLIN) != 0) {
			uint8_t bytes[128];
			ssize_t count = read(uart, bytes, sizeof(bytes));

			if (count < 0 && errno != EAGAIN && errno != EINTR) {
				perror("UART read");
				break;
			}
			for (ssize_t i = 0; i < count; i++) {
				struct uart_frame status;

				if (uart_frame_feed(&parser, bytes[i], &status) &&
				    status.type == UART_FRAME_STATUS) {
					uint16_t advance = status.sequence - status_sequence;

					now = now_ms();
					if (!have_status || now - last_status >= UART_LINK_TIMEOUT_MS ||
					    (advance != 0U && advance < 0x8000U)) {
						last_status = now;
						status_sequence = status.sequence;
						status_faults = status.flags;
						(void)uart_current_decode(status.payload, &currents);
						have_status = true;
					}
				}
			}
		}
		if ((inputs[0].revents & POLLIN) != 0) {
			uint8_t bytes[UDP_SIZE + 1];
			ssize_t count = recvfrom(sock, bytes, sizeof(bytes), MSG_TRUNC, NULL, NULL);
			struct uart_frame candidate = command;

			if (count < 0) {
				if (errno == EAGAIN || errno == EINTR) {
					continue;
				}
				perror("UDP receive");
				break;
			}
			if (decode_wheel(bytes, count, &candidate)) {
				uint32_t sequence = get_u32(bytes);
				uint32_t advance = sequence - wheel_sequence;

				now = now_ms();
				if (!have_wheel || now - last_wheel >= PI_WHEEL_TIMEOUT_MS ||
				    (advance != 0U && advance < UINT32_C(0x80000000))) {
					memcpy(command.payload, candidate.payload, UART_PAYLOAD_SIZE);
					last_wheel = now;
					wheel_sequence = sequence;
					have_wheel = true;
				}
			}
		}
	}
	/* Restore terminal flags on termination or an I/O error. */
	if (fcntl(STDOUT_FILENO, F_SETFL, stdout_flags) < 0) {
		perror("restore diagnostics");
	}
	close(sock);
	close(uart);
	return keep_running != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
