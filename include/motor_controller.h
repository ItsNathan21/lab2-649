/** @file motor_controller.h
 * @brief Encoder speed PID configuration and motor-controller API.
 */
#ifndef MOTOR_CONTROLLER_H_
#define MOTOR_CONTROLLER_H_

#include <stdbool.h>
#include <stdint.h>
#include "uart_protocol.h"

/** @brief Full-throttle wheel speed; 120 RPM is an unmeasured initial tuning value. */
#define MOTOR_PID_MAX_RPM 120U
/** @brief Update at 50 Hz; 20 ms is the initial control-loop sampling period. */
#define MOTOR_PID_PERIOD_MS 20U
/** @brief Expire commands after three missed 20 ms updates, matching the UART watchdog. */
#define MOTOR_PID_COMMAND_TIMEOUT_MS UART_LINK_TIMEOUT_MS
/** @brief Raise target speed by 120 RPM/s initially; decreases bypass this ramp. */
#define MOTOR_PID_ACCEL_RPM_PER_SEC 120U
/** @brief Speed low-pass time constant; 80 ms initially smooths encoder quantization. */
#define MOTOR_PID_SPEED_FILTER_MS 80U
/** @brief Derivative low-pass time constant; 40 ms initially suppresses edge noise. */
#define MOTOR_PID_D_FILTER_MS 40U

/* Gain macros are real gains multiplied by 1000; tune each motor independently.
 * Kp units: duty-permille / RPM. Ki: duty-permille / (RPM*s).
 * Kd: duty-permille*s / RPM. Example: KP_MILLI=4000 means Kp=4.0.
 */
/** @brief Initial left Kp=4.0 permille/RPM; unmeasured starting gain. */
#define MOTOR_PID_LEFT_KP_MILLI 4000
/** @brief Initial left Ki=0.8 permille/(RPM*s); unmeasured starting gain. */
#define MOTOR_PID_LEFT_KI_MILLI 800
/** @brief Initial left Kd=0.02 permille*s/RPM; unmeasured starting gain. */
#define MOTOR_PID_LEFT_KD_MILLI 20
/** @brief Initial right Kp=4.0 permille/RPM; tune separately from the left motor. */
#define MOTOR_PID_RIGHT_KP_MILLI 4000
/** @brief Initial right Ki=0.8 permille/(RPM*s); tune separately from the left motor. */
#define MOTOR_PID_RIGHT_KI_MILLI 800
/** @brief Initial right Kd=0.02 permille*s/RPM; tune separately from the left motor. */
#define MOTOR_PID_RIGHT_KD_MILLI 20

/** @brief Allow up to 100% PWM, retaining the previous full-throttle output limit. */
#define MOTOR_PID_MAX_DUTY_PERMILLE 1000
/** @brief Limit integral contribution to +/-40% duty; initial anti-windup bound. */
#define MOTOR_PID_I_LIMIT_PERMILLE 400
/** @brief Feedforward offset is 50% duty from the observed starting threshold. */
#define MOTOR_PID_FF_START_PERMILLE 500
/** @brief Add up to 30% duty with speed demand; initial estimate leaves PID headroom. */
#define MOTOR_PID_FF_SPAN_PERMILLE 300
/** @brief Negate left feedback: observed counts decrease under the forward drive command. */
#define MOTOR_PID_LEFT_ENCODER_SIGN (-1)
/** @brief Forward encoder sign; change to -1 if forward right counts decrease. */
#define MOTOR_PID_RIGHT_ENCODER_SIGN 1

/** @brief Treat under 2 RPM as near-stationary; initial stall detection threshold. */
#define MOTOR_PID_STALL_RPM 2U
/** @brief Check for stalls above 5 RPM demand; initial allowance for very slow commands. */
#define MOTOR_PID_STALL_TARGET_RPM 5U
/** @brief Check for stalls from 50% duty, matching the observed starting effort. */
#define MOTOR_PID_STALL_DUTY_PERMILLE 500U
/** @brief Stop both motors after 1500 ms near-stationary under drive; initial tuning value. */
#define MOTOR_PID_STALL_TIMEOUT_MS 1500U
/** @brief Reject sustained reverse feedback below -5 RPM; initial direction-check threshold. */
#define MOTOR_PID_REVERSE_RPM 5U
/** @brief Allow 200 ms of reverse feedback before stopping; initial noise grace interval. */
#define MOTOR_PID_REVERSE_TIMEOUT_MS 200U
/** @brief More than eight invalid transitions per sample faults; initial encoder-noise limit. */
#define MOTOR_PID_MAX_INVALID_EDGES 8U
/** @brief Reject samples above 10000 RPM as implausible; initial arithmetic/sensor guard. */
#define MOTOR_PID_MAX_FEEDBACK_RPM 10000U
/** @brief Log PID target, feedback, and duty at 5 Hz to keep console traffic modest. */
#define MOTOR_PID_PRINT_MS 200U
/** @brief Stack bytes; 2048 is an initial budget for PID math and diagnostics. */
#define MOTOR_PID_STACK_SIZE 2048
/** @brief Priority 4 runs PID below priority-3 fault handling and above routine reporting. */
#define MOTOR_PID_PRIORITY 4

/** @brief Fixed-point gains; each real gain is multiplied by 1000. */
struct motor_pid_gains {
	int32_t kp;
	int32_t ki;
	int32_t kd;
};

/** @brief Private controller instances use these per-wheel accumulator fields. */
struct motor_pid_state {
	int64_t speed_mrpm;
	int64_t derivative_mrpm_per_s;
	int64_t integral;
	int64_t stall_ms;
	int64_t reverse_ms;
	uint32_t duty;
};

/**
 * @brief Start the speed-control worker after motor and encoder initialization.
 * @return 0 on success, or -EALREADY if already started.
 */
int motor_controller_start(void);

/**
 * @brief Submit speed and braking together; nonzero braking overrides forward drive.
 * @param target_mrpm Requested wheel speed in millirpm, up to MOTOR_PID_MAX_RPM * 1000.
 * @param brake_raw Electrical brake duty in 0..65535; zero releases pedal braking.
 * @param received_ms Actual packet reception uptime; repeated calls do not refresh its age.
 * @return 0 on success, or -EINVAL, -ESTALE, -ENODEV, or -EIO for rejected commands.
 */
int motor_controller_set_target(uint32_t target_mrpm, uint32_t brake_raw, int64_t received_ms);

/**
 * @brief Latch the controller off and brake both motors; reset is required to restart.
 * @return 0 on successful brake, or a negative motor-driver error.
 */
int motor_controller_stop(void);

/**
 * @brief Temporarily brake; a fresh accepted target releases this inhibition.
 * @return 0 on success, or a negative motor-driver error.
 */
int motor_controller_inhibit(void);
/**
 * @brief Query whether a permanent controller or motor fault needs reset.
 * @return True for a latched hardware/PID fault.
 */
bool motor_controller_faulted(void);

#endif /* MOTOR_CONTROLLER_H_ */
