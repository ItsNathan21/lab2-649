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

/* Course UDP format: 4-byte counter + 272-byte DIJOYSTATE2.
 * State offsets: lX=0, lY=4, lRz=20, rgbButtons=48.
 * UART format: int16 steering, int16 throttle, int16 brake, uint16 buttons,
 * all little-endian (8 bytes total).
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
	int fd = open(path, O_RDWR | O_NOCTTY);
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

/**
 * @brief Write a complete packet, retrying interrupted writes.
 * @param fd Open UART descriptor.
 * @param p Source packet bytes.
 * @param length Number of bytes to send.
 * @return 0 on success, or -1 on failure; a prefix may have been sent.
 */
static int write_all(int fd, const uint8_t *p, size_t length)
{
	size_t sent = 0;
	while (sent < length) {
		ssize_t n = write(fd, p + sent, length - sent);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n <= 0) {
			if (n == 0) {
				errno = EIO;
			}
			return -1;
		}
		sent += (size_t)n;
	}
	return 0;
}

/**
 * @brief Forward UDP wheel packets to UART and print each datagram.
 * @param argc Argument count; must be two.
 * @param argv Program name and UART device pathname.
 * @return EXIT_FAILURE on invalid arguments or an I/O error.
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
	char *resolved = realpath(argv[1], NULL);
	printf("UART: %s -> %s, fd=%d, 115200 8N1, no flow control\n",
		   argv[1], resolved ? resolved : "(path unresolved)", uart);
	free(resolved);

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		close(uart);
		return EXIT_FAILURE;
	}
	struct sockaddr_in local = {
		.sin_family = AF_INET,
		.sin_port = htons(UDP_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
		perror("bind UDP");
		close(sock);
		close(uart);
		return EXIT_FAILURE;
	}
	printf("Listening on UDP 0.0.0.0:%d\n", UDP_PORT);

	for (;;) {
		uint8_t udp[UDP_SIZE + 1];
		struct sockaddr_in peer = {0};
		socklen_t peer_len = sizeof(peer);
		ssize_t n = recvfrom(sock, udp, sizeof(udp), MSG_TRUNC,
							 (struct sockaddr *)&peer, &peer_len);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("recvfrom");
			break;
		}
		char ip[INET_ADDRSTRLEN] = "?";
		(void)inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
		printf("UDP %s:%u bytes=%zd", ip, (unsigned int)ntohs(peer.sin_port), n);
		if (n != UDP_SIZE) {
			printf(" -> SKIPPED (expected %d)\n", UDP_SIZE);
			continue;
		}

		uint32_t sequence = get_u32(udp);
		int32_t steering = get_i32(udp + 4);
		int32_t throttle = get_i32(udp + 8);
		int32_t brake = get_i32(udp + 24);
		uint16_t buttons = 0;
		static const unsigned int indices[] = {0, 1, 2, 3, 4, 5, 8, 9};
		for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
			if (udp[52 + indices[i]] & 0x80U) {
				buttons |= (uint16_t)(1U << indices[i]);
			}
		}
		printf(" seq=%" PRIu32 " steering=%" PRId32 " throttle=%" PRId32
			   " brake=%" PRId32 " buttons=0x%04x\n",
			   sequence, steering, throttle, brake, (unsigned int)buttons);
		if (steering < INT16_MIN || steering > INT16_MAX ||
			throttle < INT16_MIN || throttle > INT16_MAX ||
			brake < INT16_MIN || brake > INT16_MAX) {
			printf("UART SKIPPED: axis outside signed 16-bit range\n");
			continue;
		}

		uint8_t packet[8];
		put_u16(packet + 0, (uint16_t)steering);
		put_u16(packet + 2, (uint16_t)throttle);
		put_u16(packet + 4, (uint16_t)brake);
		put_u16(packet + 6, buttons);
		if (write_all(uart, packet, sizeof(packet)) < 0) {
			perror("write UART (stopping after possible partial packet)");
			break;
		}
		printf("UART %s: wrote 8 bytes:", argv[1]);
		for (size_t i = 0; i < sizeof(packet); ++i) {
			printf(" %02x", (unsigned int)packet[i]);
		}
		printf("\n");
	}
	close(sock);
	close(uart);
	return EXIT_FAILURE;
}
