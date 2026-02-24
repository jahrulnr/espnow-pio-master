#include "joystick_manager.h"

#include <app_config.h>

namespace {
static constexpr int kCenterCalibrationSamples = 48;
static constexpr int kCenterTrackWindowRaw = 70;
static constexpr uint16_t kNeutralSampleTarget = 120;
static constexpr int kNeutralSampleWindow = 80;
static constexpr int kNeutralTrackWindow = 24;
}

JoystickManager::JoystickManager(int maxJoysticks) {
    if (maxJoysticks < 1) {
        this->maxJoysticks = 1;
    } else if (maxJoysticks > kMaxJoystickSlots) {
        this->maxJoysticks = kMaxJoystickSlots;
    } else {
        this->maxJoysticks = maxJoysticks;
    }

    this->joystickCount = 0;
    
    // Set default settings
    deadzoneThreshold = DEADZONE_THRESHOLD;
    directionThreshold = DIRECTION_THRESHOLD;
    debounceDelay = SWITCH_DEBOUNCE_MS;
    kalmanEnabled = true;
    kalmanProcessNoise = 0.08f;
    kalmanMeasurementNoise = 6.0f;
    centerTrackAlphaShift = 5;
    
    // Initialize arrays
    for (int i = 0; i < kMaxJoystickSlots; i++) {
        joysticks[i] = {0};
        pins[i] = {-1, -1, -1};
    }
}

JoystickManager::~JoystickManager() {
}

bool JoystickManager::addJoystick(int vrxPin, int vryPin, int swPin) {
    if (joystickCount >= maxJoysticks) {
        return false;
    }
    
    pins[joystickCount] = {vrxPin, vryPin, swPin};
    
    // Initialize joystick data
    joysticks[joystickCount].rawX = 0;
    joysticks[joystickCount].rawY = 0;
    joysticks[joystickCount].normalizedX = 0;
    joysticks[joystickCount].normalizedY = 0;
    joysticks[joystickCount].rotatedX = 0;
    joysticks[joystickCount].rotatedY = 0;
    joysticks[joystickCount].direction = JOYSTICK_CENTER;
    joysticks[joystickCount].switchPressed = false;
    joysticks[joystickCount].switchChanged = false;
    joysticks[joystickCount].lastSwitchChange = 0;
    joysticks[joystickCount].lastSwitchState = true; // Pullup
    
    // Initialize calibration with default values
    joysticks[joystickCount].centerX = CENTER_VALUE;
    joysticks[joystickCount].centerY = CENTER_VALUE;
    joysticks[joystickCount].minX = 0;
    joysticks[joystickCount].maxX = ANALOG_RESOLUTION - 1;
    joysticks[joystickCount].minY = 0;
    joysticks[joystickCount].maxY = ANALOG_RESOLUTION - 1;
    joysticks[joystickCount].calibrated = false;
    
    // Initialize rotation and orientation
    joysticks[joystickCount].rotation = JOYSTICK_ROTATION_0;
    joysticks[joystickCount].invertX = false;
    joysticks[joystickCount].invertY = true;
    joysticks[joystickCount].kalmanX = 0.0f;
    joysticks[joystickCount].kalmanY = 0.0f;
    joysticks[joystickCount].kalmanErrorX = 1.0f;
    joysticks[joystickCount].kalmanErrorY = 1.0f;
    joysticks[joystickCount].kalmanReady = false;
    joysticks[joystickCount].neutralOffsetX = 0;
    joysticks[joystickCount].neutralOffsetY = 0;
    joysticks[joystickCount].neutralAccumX = 0;
    joysticks[joystickCount].neutralAccumY = 0;
    joysticks[joystickCount].neutralSamples = 0;
    joysticks[joystickCount].neutralReady = false;
    
    joystickCount++;
    
    return true;
}

bool JoystickManager::addJoystick(JoystickPin pinConfig) {
    return addJoystick(pinConfig.vrx, pinConfig.vry, pinConfig.sw);
}

void JoystickManager::removeJoystick(int index) {
    if (!isValidIndex(index)) return;
    
    // Shift all joysticks after this index down
    for (int i = index; i < joystickCount - 1; i++) {
        joysticks[i] = joysticks[i + 1];
        pins[i] = pins[i + 1];
    }
    
    joystickCount--;
}

void JoystickManager::init() {    
    for (int i = 0; i < joystickCount; i++) {
        if (pins[i].sw != -1) {
            pinMode(pins[i].sw, INPUT_PULLUP);
        }

        long sumX = 0;
        long sumY = 0;
        for (int sample = 0; sample < kCenterCalibrationSamples; sample++) {
            sumX += analogRead(pins[i].vrx);
            sumY += analogRead(pins[i].vry);
            delay(2);
        }

        joysticks[i].centerX = static_cast<int>(sumX / kCenterCalibrationSamples);
        joysticks[i].centerY = static_cast<int>(sumY / kCenterCalibrationSamples);
        joysticks[i].kalmanReady = false;
        joysticks[i].neutralOffsetX = 0;
        joysticks[i].neutralOffsetY = 0;
        joysticks[i].neutralAccumX = 0;
        joysticks[i].neutralAccumY = 0;
        joysticks[i].neutralSamples = 0;
        joysticks[i].neutralReady = false;
        joysticks[i].calibrated = true;
    }
}

void JoystickManager::update() {
    for (int i = 0; i < joystickCount; i++) {
        updateJoystickData(i);
        updateSwitchState(i);
    }
}

void JoystickManager::updateJoystickData(int index) {
    if (!isValidIndex(index)) return;
    
    // Read raw analog values
    joysticks[index].rawX = analogRead(pins[index].vrx);
    joysticks[index].rawY = analogRead(pins[index].vry);

    if (abs(joysticks[index].rawX - joysticks[index].centerX) <= kCenterTrackWindowRaw) {
        joysticks[index].centerX = ((joysticks[index].centerX * ((1 << centerTrackAlphaShift) - 1)) + joysticks[index].rawX) >> centerTrackAlphaShift;
    }
    if (abs(joysticks[index].rawY - joysticks[index].centerY) <= kCenterTrackWindowRaw) {
        joysticks[index].centerY = ((joysticks[index].centerY * ((1 << centerTrackAlphaShift) - 1)) + joysticks[index].rawY) >> centerTrackAlphaShift;
    }
    
    // Normalize values to -100 to 100 range
    joysticks[index].normalizedX = normalizeValue(
        joysticks[index].rawX,
        joysticks[index].centerX,
        joysticks[index].minX,
        joysticks[index].maxX
    );
    
    joysticks[index].normalizedY = normalizeValue(
        joysticks[index].rawY,
        joysticks[index].centerY,
        joysticks[index].minY,
        joysticks[index].maxY
    );

    if (kalmanEnabled) {
        joysticks[index].normalizedX = applyKalmanFilter(index, joysticks[index].normalizedX, true);
        joysticks[index].normalizedY = applyKalmanFilter(index, joysticks[index].normalizedY, false);
    }

    JoystickData& joy = joysticks[index];

    if (!joy.neutralReady) {
        if (abs(joy.normalizedX) <= kNeutralSampleWindow && abs(joy.normalizedY) <= kNeutralSampleWindow) {
            joy.neutralAccumX += joy.normalizedX;
            joy.neutralAccumY += joy.normalizedY;
            joy.neutralSamples++;
        }

        if (joy.neutralSamples >= kNeutralSampleTarget) {
            joy.neutralOffsetX = static_cast<int>(joy.neutralAccumX / joy.neutralSamples);
            joy.neutralOffsetY = static_cast<int>(joy.neutralAccumY / joy.neutralSamples);
            joy.neutralReady = true;
        }
    }

    if (joy.neutralReady) {
        const int preOffsetX = joy.normalizedX;
        const int preOffsetY = joy.normalizedY;

        joy.normalizedX = constrain(preOffsetX - joy.neutralOffsetX, -100, 100);
        joy.normalizedY = constrain(preOffsetY - joy.neutralOffsetY, -100, 100);

        if (abs(preOffsetX) <= kNeutralTrackWindow) {
            joy.neutralOffsetX = (joy.neutralOffsetX * 31 + preOffsetX) / 32;
        }
        if (abs(preOffsetY) <= kNeutralTrackWindow) {
            joy.neutralOffsetY = (joy.neutralOffsetY * 31 + preOffsetY) / 32;
        }
    }
    
    // Apply rotation and inversion
    applyRotation(index);
    
    // Calculate direction using rotated values
    joysticks[index].direction = calculateDirection(
        joysticks[index].rotatedX,
        joysticks[index].rotatedY,
        index
    );
}

void JoystickManager::updateSwitchState(int index) {
    if (!isValidIndex(index) || pins[index].sw == -1) return;
    
    unsigned long currentTime = millis();
    bool currentSwitchState = digitalRead(pins[index].sw);
    
    // Detect state change
    if (currentSwitchState != joysticks[index].lastSwitchState) {
        joysticks[index].lastSwitchChange = currentTime;
        joysticks[index].lastSwitchState = currentSwitchState;
        joysticks[index].switchChanged = true;
    } else {
        joysticks[index].switchChanged = false;
    }
    
    // Debounce and update switch state
    if ((currentTime - joysticks[index].lastSwitchChange) > debounceDelay) {
        joysticks[index].switchPressed = !currentSwitchState; // Inverted because of pullup
    }
}

int JoystickManager::normalizeValue(int raw, int center, int min, int max) {
    int normalized;
    
    if (raw > center) {
        // Map from center to max -> 0 to 100
        if (max == center) return 0;
        normalized = map(raw, center, max, 0, 100);
    } else {
        // Map from min to center -> -100 to 0
        if (min == center) return 0;
        normalized = map(raw, min, center, -100, 0);
    }
    
    // Apply deadzone
    if (abs(normalized) <= deadzoneThreshold * 100 / ANALOG_RESOLUTION) {
        normalized = 0;
    }
    
    return constrain(normalized, -100, 100);
}

int JoystickManager::applyKalmanFilter(int index, int measurement, bool isXAxis) {
    if (!isValidIndex(index)) {
        return measurement;
    }

    JoystickData& joy = joysticks[index];
    float& estimate = isXAxis ? joy.kalmanX : joy.kalmanY;
    float& error = isXAxis ? joy.kalmanErrorX : joy.kalmanErrorY;

    if (!joy.kalmanReady) {
        estimate = static_cast<float>(measurement);
        error = 1.0f;
        joy.kalmanReady = true;
        return measurement;
    }

    error += kalmanProcessNoise;
    const float gain = error / (error + kalmanMeasurementNoise);
    estimate = estimate + gain * (static_cast<float>(measurement) - estimate);
    error = (1.0f - gain) * error;

    int filtered = static_cast<int>(estimate);
    if (abs(filtered) <= 2) {
        filtered = 0;
    }

    return constrain(filtered, -100, 100);
}

int JoystickManager::calculateDirection(int rotatedX, int rotatedY, int index) {
    // Apply directional threshold
    int threshold = directionThreshold * 100 / ANALOG_RESOLUTION;
    
    bool isUpDir = rotatedY > threshold;
    bool isDownDir = rotatedY < -threshold;
    bool isLeftDir = rotatedX < -threshold;
    bool isRightDir = rotatedX > threshold;
    
    // Determine direction
    if (isUpDir && isLeftDir) return JOYSTICK_UP_LEFT;
    if (isUpDir && isRightDir) return JOYSTICK_UP_RIGHT;
    if (isDownDir && isLeftDir) return JOYSTICK_DOWN_LEFT;
    if (isDownDir && isRightDir) return JOYSTICK_DOWN_RIGHT;
    if (isUpDir) return JOYSTICK_UP;
    if (isDownDir) return JOYSTICK_DOWN;
    if (isLeftDir) return JOYSTICK_LEFT;
    if (isRightDir) return JOYSTICK_RIGHT;
    
    return JOYSTICK_CENTER;
}

bool JoystickManager::isValidIndex(int index) {
    if (index < 0 || index >= joystickCount) {
        return false;
    }
    return true;
}

// Data access methods
JoystickData JoystickManager::getJoystickData(int index) {
    if (!isValidIndex(index)) {
        return {0}; // Return empty data
    }
    return joysticks[index];
}

int JoystickManager::getRawX(int index) {
    return isValidIndex(index) ? joysticks[index].rawX : 0;
}

int JoystickManager::getRawY(int index) {
    return isValidIndex(index) ? joysticks[index].rawY : 0;
}

int JoystickManager::getNormalizedX(int index) {
    return isValidIndex(index) ? joysticks[index].normalizedX : 0;
}

int JoystickManager::getNormalizedY(int index) {
    return isValidIndex(index) ? joysticks[index].normalizedY : 0;
}

int JoystickManager::getRotatedX(int index) {
    return isValidIndex(index) ? joysticks[index].rotatedX : 0;
}

int JoystickManager::getRotatedY(int index) {
    return isValidIndex(index) ? joysticks[index].rotatedY : 0;
}

int JoystickManager::getDirection(int index) {
    return isValidIndex(index) ? joysticks[index].direction : JOYSTICK_CENTER;
}

// Switch methods
bool JoystickManager::isSwitchPressed(int index) {
    return isValidIndex(index) ? joysticks[index].switchPressed : false;
}

bool JoystickManager::wasSwitchPressed(int index) {
    return isValidIndex(index) ? 
        (joysticks[index].switchPressed && joysticks[index].switchChanged) : false;
}

bool JoystickManager::wasSwitchReleased(int index) {
    return isValidIndex(index) ? 
        (!joysticks[index].switchPressed && joysticks[index].switchChanged) : false;
}

void JoystickManager::clearSwitchState(int index) {
    if (isValidIndex(index)) {
        joysticks[index].switchChanged = false;
    }
}

// Direction helpers
bool JoystickManager::isUp(int index) {
    int dir = getDirection(index);
    return (dir == JOYSTICK_UP || dir == JOYSTICK_UP_LEFT || dir == JOYSTICK_UP_RIGHT);
}

bool JoystickManager::isDown(int index) {
    int dir = getDirection(index);
    return (dir == JOYSTICK_DOWN || dir == JOYSTICK_DOWN_LEFT || dir == JOYSTICK_DOWN_RIGHT);
}

bool JoystickManager::isLeft(int index) {
    int dir = getDirection(index);
    return (dir == JOYSTICK_LEFT || dir == JOYSTICK_UP_LEFT || dir == JOYSTICK_DOWN_LEFT);
}

bool JoystickManager::isRight(int index) {
    int dir = getDirection(index);
    return (dir == JOYSTICK_RIGHT || dir == JOYSTICK_UP_RIGHT || dir == JOYSTICK_DOWN_RIGHT);
}

bool JoystickManager::isCenter(int index) {
    return getDirection(index) == JOYSTICK_CENTER;
}

bool JoystickManager::isDiagonal(int index) {
    int dir = getDirection(index);
    return (dir == JOYSTICK_UP_LEFT || dir == JOYSTICK_UP_RIGHT || 
            dir == JOYSTICK_DOWN_LEFT || dir == JOYSTICK_DOWN_RIGHT);
}

bool JoystickManager::isPressed(int index) {
    if (!isValidIndex(index)) return false;
    return getDirection(index) != JOYSTICK_CENTER || joysticks[index].switchPressed;
}

// Calibration methods
void JoystickManager::startCalibration(int index) {
    if (!isValidIndex(index)) return;
    
    joysticks[index].minX = ANALOG_RESOLUTION;
    joysticks[index].maxX = 0;
    joysticks[index].minY = ANALOG_RESOLUTION;
    joysticks[index].maxY = 0;
    joysticks[index].calibrated = false;
}

void JoystickManager::calibrateCenter(int index) {
    if (!isValidIndex(index)) return;
    
    joysticks[index].centerX = analogRead(pins[index].vrx);
    joysticks[index].centerY = analogRead(pins[index].vry);
}

void JoystickManager::finishCalibration(int index) {
    if (!isValidIndex(index)) return;
    
    joysticks[index].calibrated = true;
}

void JoystickManager::autoCalibrate(int index, unsigned long duration) {
    if (!isValidIndex(index)) return;
    
    startCalibration(index);
    
    unsigned long startTime = millis();
    while (millis() - startTime < duration) {
        int x = analogRead(pins[index].vrx);
        int y = analogRead(pins[index].vry);
        
        if (x < joysticks[index].minX) joysticks[index].minX = x;
        if (x > joysticks[index].maxX) joysticks[index].maxX = x;
        if (y < joysticks[index].minY) joysticks[index].minY = y;
        if (y > joysticks[index].maxY) joysticks[index].maxY = y;
        
        delay(10);
    }
    
    calibrateCenter(index);
    finishCalibration(index);
}

bool JoystickManager::isCalibrated(int index) {
    return isValidIndex(index) ? joysticks[index].calibrated : false;
}

void JoystickManager::resetCalibration(int index) {
    if (!isValidIndex(index)) return;
    
    joysticks[index].centerX = CENTER_VALUE;
    joysticks[index].centerY = CENTER_VALUE;
    joysticks[index].minX = 0;
    joysticks[index].maxX = ANALOG_RESOLUTION - 1;
    joysticks[index].minY = 0;
    joysticks[index].maxY = ANALOG_RESOLUTION - 1;
    joysticks[index].calibrated = false;
    joysticks[index].kalmanReady = false;
    joysticks[index].neutralOffsetX = 0;
    joysticks[index].neutralOffsetY = 0;
    joysticks[index].neutralAccumX = 0;
    joysticks[index].neutralAccumY = 0;
    joysticks[index].neutralSamples = 0;
    joysticks[index].neutralReady = false;
}

// Pin management
bool JoystickManager::setJoystickPins(int index, int vrxPin, int vryPin, int swPin) {
    if (!isValidIndex(index)) return false;
    
    pins[index] = {vrxPin, vryPin, swPin};
    
    if (swPin != -1) {
        pinMode(swPin, INPUT_PULLUP);
    }
    
    return true;
}

JoystickPin JoystickManager::getJoystickPins(int index) {
    if (!isValidIndex(index)) {
        return {-1, -1, -1};
    }
    return pins[index];
}

// Convenience setup methods
void JoystickManager::setupDefaultTwoJoysticks() {
	setupMirroredJoysticks();
}

void JoystickManager::setupSingleJoystick(int vrxPin, int vryPin, int swPin) {
    addJoystick(vrxPin, vryPin, swPin);
}

// Convenience setup with rotation
void JoystickManager::setupJoystickWithRotation(int vrxPin, int vryPin, int swPin, int rotation) {
    addJoystick(vrxPin, vryPin, swPin);
    setRotation(joystickCount - 1, rotation); // Set rotation for the just-added joystick
}

void JoystickManager::setupTwoJoysticksWithRotation(int rotation1, int rotation2) {
    addJoystick(INPUT_JOYSTICK1_VRX_PIN, INPUT_JOYSTICK1_VRY_PIN, INPUT_JOYSTICK1_SW_PIN);
    setRotation(0, rotation1);
    
    addJoystick(INPUT_JOYSTICK2_VRX_PIN, INPUT_JOYSTICK2_VRY_PIN, INPUT_JOYSTICK2_SW_PIN);
    setRotation(1, rotation2);
}

void JoystickManager::setupMirroredJoysticks() {
    addJoystick(INPUT_JOYSTICK1_VRX_PIN, INPUT_JOYSTICK1_VRY_PIN, INPUT_JOYSTICK1_SW_PIN);
    setRotation(0, JOYSTICK_ROTATION_180);
    addJoystick(INPUT_JOYSTICK2_VRX_PIN, INPUT_JOYSTICK2_VRY_PIN, INPUT_JOYSTICK2_SW_PIN);
}

// Debug methods
void JoystickManager::printDebugInfo(int index) {
    if (!isValidIndex(index)) return;
    
    JoystickData& joy = joysticks[index];
    if (joy.calibrated) {
    }
    if (joy.rotation != 0) {
    }
    if (joy.invertX || joy.invertY) {
    }
}

void JoystickManager::printAllDebugInfo() {
    for (int i = 0; i < joystickCount; i++) {
        printDebugInfo(i);
    }
}

void JoystickManager::printConfiguration() {
    
    for (int i = 0; i < joystickCount; i++) {
        if (joysticks[i].invertX || joysticks[i].invertY) {
        }
    }
}

// Rotation and orientation methods
void JoystickManager::applyRotation(int index) {
    if (!isValidIndex(index)) return;
    
    int x = joysticks[index].normalizedX;
    int y = joysticks[index].normalizedY;
    
    // Apply inversion first
    if (joysticks[index].invertX) x = -x;
    if (joysticks[index].invertY) y = -y;
    
    // Apply rotation
    switch (joysticks[index].rotation) {
        case JOYSTICK_ROTATION_0:
            joysticks[index].rotatedX = x;
            joysticks[index].rotatedY = y;
            break;
            
        case JOYSTICK_ROTATION_90:
            joysticks[index].rotatedX = -y;
            joysticks[index].rotatedY = x;
            break;
            
        case JOYSTICK_ROTATION_180:
            joysticks[index].rotatedX = -x;
            joysticks[index].rotatedY = -y;
            break;
            
        case JOYSTICK_ROTATION_270:
            joysticks[index].rotatedX = y;
            joysticks[index].rotatedY = -x;
            break;
            
        default:
            // Invalid rotation, use no rotation
            joysticks[index].rotatedX = x;
            joysticks[index].rotatedY = y;
            break;
    }
}

void JoystickManager::setRotation(int index, int degrees) {
    if (!isValidIndex(index)) return;
    
    // Normalize degrees to 0, 90, 180, 270
    degrees = degrees % 360;
    if (degrees < 0) degrees += 360;
    
    // Round to nearest 90 degrees
    if (degrees < 45) degrees = 0;
    else if (degrees < 135) degrees = 90;
    else if (degrees < 225) degrees = 180;
    else if (degrees < 315) degrees = 270;
    else degrees = 0;
    
    joysticks[index].rotation = degrees;
}

int JoystickManager::getRotation(int index) {
    return isValidIndex(index) ? joysticks[index].rotation : 0;
}

void JoystickManager::setInvertX(int index, bool invert) {
    if (!isValidIndex(index)) return;
    
    joysticks[index].invertX = invert;
}

void JoystickManager::setInvertY(int index, bool invert) {
    if (!isValidIndex(index)) return;
    
    joysticks[index].invertY = invert;
}

bool JoystickManager::isXInverted(int index) {
    return isValidIndex(index) ? joysticks[index].invertX : false;
}

bool JoystickManager::isYInverted(int index) {
    return isValidIndex(index) ? joysticks[index].invertY : false;
}

void JoystickManager::resetOrientation(int index) {
    if (!isValidIndex(index)) return;
    
    joysticks[index].rotation = JOYSTICK_ROTATION_0;
    joysticks[index].invertX = false;
    joysticks[index].invertY = false;
}
