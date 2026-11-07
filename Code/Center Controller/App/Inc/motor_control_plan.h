#ifndef MOTOR_CONTROL_PLAN_H
#define MOTOR_CONTROL_PLAN_H
/*
 * Future motion integration outline (intentionally no callable drive API):
 * 1. Confirm C620 ID, original gearbox, belt 40/110, sign and shaft speed units.
 * 2. Add motor feedback freshness, speed estimator and latched fault handling.
 * 3. Validate current limits, speed loop and acceleration/deceleration on hardware.
 * 4. Add STOPPING with measured standstill; never infer stop from CAN silence.
 * 5. Add small/big rune speed profiles and direction transition state machine.
 * 6. Only then enable the motor CAN TX boundary and advertise motion capability.
 * 7. Any required leaf loss aborts the round and requests a verified stop.
 *
 * Current firmware: receives 0x201..0x208 only. Center CAN TX is still restricted
 * to 0x500. ENTER_DEBUG/CALIBRATE cannot bypass missing or moving feedback.
 */
#endif
