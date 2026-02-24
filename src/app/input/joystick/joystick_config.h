#ifndef JOYSTICK_CONFIG_H
#define JOYSTICK_CONFIG_H

// Analog Configuration
#define ANALOG_RESOLUTION    4096    // 12-bit ADC (0-4095)
#define CENTER_VALUE         2048    // Middle value for centering
#define DEADZONE_THRESHOLD   400     // Deadzone around center
#define DIRECTION_THRESHOLD  1000    // Threshold for direction detection

// Debounce Configuration
#define SWITCH_DEBOUNCE_MS   50      // Switch debounce time

// Direction constants
#define JOYSTICK_CENTER    0
#define JOYSTICK_UP        1
#define JOYSTICK_DOWN      2
#define JOYSTICK_LEFT      3
#define JOYSTICK_RIGHT     4
#define JOYSTICK_UP_LEFT   5
#define JOYSTICK_UP_RIGHT  6
#define JOYSTICK_DOWN_LEFT 7
#define JOYSTICK_DOWN_RIGHT 8

// Rotation constants (in degrees)
#define JOYSTICK_ROTATION_0    0    // No rotation
#define JOYSTICK_ROTATION_90   90   // 90° clockwise
#define JOYSTICK_ROTATION_180  180  // 180° rotation
#define JOYSTICK_ROTATION_270  270  // 270° clockwise (90° counter-clockwise)

#endif // JOYSTICK_CONFIG_H
