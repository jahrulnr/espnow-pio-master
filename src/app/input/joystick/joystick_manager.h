#ifndef JOYSTICK_MANAGER_H
#define JOYSTICK_MANAGER_H

#include <Arduino.h>
#include "joystick_config.h"

struct JoystickPin {
    int vrx;
    int vry;
    int sw;
};

struct JoystickData {
    int rawX;           // Raw X value (0-4095)
    int rawY;           // Raw Y value (0-4095)
    int normalizedX;    // Normalized X value (-100 to 100)
    int normalizedY;    // Normalized Y value (-100 to 100)
    int rotatedX;       // Rotated X value (-100 to 100)
    int rotatedY;       // Rotated Y value (-100 to 100)
    int direction;      // Current direction (JOYSTICK_CENTER, UP, DOWN, etc.)
    bool switchPressed; // Switch button state
    bool switchChanged; // Switch state changed this frame
    unsigned long lastSwitchChange; // Last switch state change time
    bool lastSwitchState; // Previous switch state
    
    // Calibration data
    int centerX;
    int centerY;
    int minX;
    int maxX;
    int minY;
    int maxY;
    bool calibrated;
    
    // Rotation data
    int rotation;       // Rotation in degrees (0, 90, 180, 270)
    bool invertX;       // Invert X axis
    bool invertY;       // Invert Y axis

    // Filter and stability data
    float kalmanX;
    float kalmanY;
    float kalmanErrorX;
    float kalmanErrorY;
    bool kalmanReady;

    // Neutral offset calibration
    int neutralOffsetX;
    int neutralOffsetY;
    int32_t neutralAccumX;
    int32_t neutralAccumY;
    uint16_t neutralSamples;
    bool neutralReady;
};

class JoystickManager {
private:
    static constexpr int kMaxJoystickSlots = 4;

    JoystickData joysticks[kMaxJoystickSlots];
    JoystickPin pins[kMaxJoystickSlots];
    int joystickCount;          // Number of active joysticks
    int maxJoysticks;           // Active capacity (<= kMaxJoystickSlots)
    
    // Global settings
    int deadzoneThreshold;
    int directionThreshold;
    unsigned long debounceDelay;
    bool kalmanEnabled;
    float kalmanProcessNoise;
    float kalmanMeasurementNoise;
    uint8_t centerTrackAlphaShift;
    
    // Helper methods
    int calculateDirection(int rotatedX, int rotatedY, int index);
    int normalizeValue(int raw, int center, int min, int max);
    void updateSwitchState(int index);
    void updateJoystickData(int index);
    void applyRotation(int index);
    int applyKalmanFilter(int index, int measurement, bool isXAxis);
    bool isValidIndex(int index);
    
public:
    JoystickManager(int maxJoysticks = 4);
    ~JoystickManager();
    
    // Initialization
    bool addJoystick(int vrxPin, int vryPin, int swPin);
    bool addJoystick(JoystickPin pinConfig);
    void removeJoystick(int index);
    void init();
    void update();
    
    // Joystick management
    int getJoystickCount() { return joystickCount; }
    bool setJoystickPins(int index, int vrxPin, int vryPin, int swPin);
    JoystickPin getJoystickPins(int index);
    
    // Data access
    JoystickData getJoystickData(int index);
    int getRawX(int index);
    int getRawY(int index);
    int getNormalizedX(int index);
    int getNormalizedY(int index);
    int getRotatedX(int index);      // Get X after rotation
    int getRotatedY(int index);      // Get Y after rotation
    int getDirection(int index);
    
    // Switch methods
    bool isSwitchPressed(int index);
    bool wasSwitchPressed(int index);
    bool wasSwitchReleased(int index);
    void clearSwitchState(int index);
    
    // Direction helpers
    bool isUp(int index);
    bool isDown(int index);
    bool isLeft(int index);
    bool isRight(int index);
    bool isCenter(int index);
    bool isDiagonal(int index);
    
    // General state check
    bool isPressed(int index); // Returns true if any direction is pressed
    
    // Rotation and orientation
    void setRotation(int index, int degrees);
    int getRotation(int index);
    void setInvertX(int index, bool invert);
    void setInvertY(int index, bool invert);
    bool isXInverted(int index);
    bool isYInverted(int index);
    void resetOrientation(int index);
    
    // Convenience rotation methods
    void setRotation0(int index)   { setRotation(index, JOYSTICK_ROTATION_0); }
    void setRotation90(int index)  { setRotation(index, JOYSTICK_ROTATION_90); }
    void setRotation180(int index) { setRotation(index, JOYSTICK_ROTATION_180); }
    void setRotation270(int index) { setRotation(index, JOYSTICK_ROTATION_270); }
    
    // Calibration
    void startCalibration(int index);
    void calibrateCenter(int index);
    void finishCalibration(int index);
    void autoCalibrate(int index, unsigned long duration = 3000);
    bool isCalibrated(int index);
    void resetCalibration(int index);
    
    // Global configuration
    void setDeadzone(int threshold) { deadzoneThreshold = threshold; }
    void setDirectionThreshold(int threshold) { directionThreshold = threshold; }
    void setDebounceDelay(unsigned long delay) { debounceDelay = delay; }
    
    int getDeadzone() { return deadzoneThreshold; }
    int getDirectionThreshold() { return directionThreshold; }
    unsigned long getDebounceDelay() { return debounceDelay; }
    void setKalmanEnabled(bool enabled) { kalmanEnabled = enabled; }
    bool isKalmanEnabled() const { return kalmanEnabled; }
    
    // Convenience methods for common setups
    void setupDefaultTwoJoysticks();
    void setupSingleJoystick(int vrxPin, int vryPin, int swPin);
    
    // Convenience setup with rotation
    void setupJoystickWithRotation(int vrxPin, int vryPin, int swPin, int rotation);
    void setupTwoJoysticksWithRotation(int rotation1, int rotation2);
    void setupMirroredJoysticks(); // One normal, one rotated 180°
    
    // Debug
    void printDebugInfo(int index);
    void printAllDebugInfo();
    void printConfiguration();
};

#endif // JOYSTICK_MANAGER_H
