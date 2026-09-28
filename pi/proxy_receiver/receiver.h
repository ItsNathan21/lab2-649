/** @file receiver.h
 * @brief Receiver API and constants.
 */
#ifndef RECEIVER_H_
#define RECEIVER_H_

#ifndef _DEFAULT_SOURCE
/** @brief Enable glibc UART helpers such as cfmakeraw before system includes. */
#define _DEFAULT_SOURCE
#endif

/** @brief Listen on port 8000 to match the wheel GUI sender configuration. */
#define UDP_PORT 8000
/** @brief Expect a 4-byte counter followed by the 272-byte course joystick state. */
#define UDP_SIZE 276

/** @brief A 100 ms wheel-age limit allows two nominal 50 ms GUI intervals. */
#define PI_WHEEL_TIMEOUT_MS 100
/** @brief Five milliseconds bounds a stuck UART write well below the link deadline. */
#define PI_UART_WRITE_TIMEOUT_MS 5
/** @brief Status diagnostics print twice per second to keep terminal traffic modest. */
#define PI_STATUS_PRINT_MS 500

/**
 * @brief Run the UDP-to-UART forwarder, printing every received datagram.
 * @param argc Argument count; exactly two are required.
 * @param argv Program name and the UART device path.
 * @return EXIT_SUCCESS on termination, or EXIT_FAILURE for arguments/I/O errors.
 */
int main(int argc, char **argv);

#endif /* RECEIVER_H_ */
