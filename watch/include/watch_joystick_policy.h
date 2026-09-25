#ifndef DOGGY_WATCH_JOYSTICK_POLICY_H
#define DOGGY_WATCH_JOYSTICK_POLICY_H

// Matches web/rover.html JOYSTICK_DEADZONE.
inline constexpr float kWatchJoystickDeadzone = 0.15f;

// ================================================================================

float watch_joystick_apply_deadzone(float value, float dead);

// ================================================================================

// Pixel offset from pad center to normalized stick (circular clamp), matching
// web/rover.html travel = pad radius.
void watch_joystick_normalize_stick_offset(
        float delta_x,
        float delta_y,
        float travel,
        float *normalized_x,
        float *normalized_y);

// ================================================================================

// Normalized stick position in [-1, 1] with a circular clamp; writes rover
// speed (forward) and turn (right positive) after per-axis dead zones.
void watch_joystick_drive_from_stick(
        float nx,
        float ny,
        float dead,
        float *speed,
        float *turn);

#endif
