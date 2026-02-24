#include "input_manager.h"

#include <esp_log.h>

namespace {
static constexpr const char* TAG = "INPUT_BTN";

const char* buttonName(ButtonIndex btn) {
    switch (btn) {
        case BTN_UP:
            return "UP";
        case BTN_DOWN:
            return "DOWN";
        case BTN_SELECT:
            return "SELECT";
        case BTN_BACK:
            return "BACK";
        default:
            return "UNKNOWN";
    }
}
}

InputManager::InputManager() {
    holdThreshold = 1000; // 1 second for hold
    
    buttonPins[BTN_UP] = INPUT_BUTTON_UP_PIN;
    buttonPins[BTN_DOWN] = INPUT_BUTTON_DOWN_PIN;
    buttonPins[BTN_SELECT] = INPUT_BUTTON_SELECT_PIN;
    buttonPins[BTN_BACK] = INPUT_BUTTON_BACK_PIN;
}

void InputManager::init() {
    for (int i = 0; i < BTN_COUNT; i++) {
        pinMode(buttonPins[i], INPUT_PULLUP);
        buttons[i].pressed = false;
        buttons[i].lastState = true;
        buttons[i].lastPress = 0;
        buttons[i].pressTime = 0;
        buttons[i].held = false;
        buttons[i].pressedEdge = false;
        buttons[i].releasedEdge = false;

        ESP_LOGI(TAG, "Button %s on GPIO%d initialized (idle=%d)",
                 buttonName(static_cast<ButtonIndex>(i)), buttonPins[i], digitalRead(buttonPins[i]));
    }
}

void InputManager::update() {
    unsigned long currentTime = millis();
    
    for (int i = 0; i < BTN_COUNT; i++) {
        buttons[i].pressedEdge = false;
        buttons[i].releasedEdge = false;

        bool currentState = digitalRead(buttonPins[i]);
        
        // Detect state change
        if (currentState != buttons[i].lastState) {
            buttons[i].lastPress = currentTime;
            buttons[i].lastState = currentState;
        }
        
        // Debounce and update button state
        if ((currentTime - buttons[i].lastPress) > kDebounceDelayMs) {
            bool newPressed = !currentState; // Inverted because of pullup
            
            if (newPressed && !buttons[i].pressed) {
                // Button just pressed
                buttons[i].pressed = true;
                buttons[i].pressTime = currentTime;
                buttons[i].held = false;
                buttons[i].pressedEdge = true;
                ESP_LOGD(TAG, "Pressed  %s (GPIO%d)", buttonName(static_cast<ButtonIndex>(i)), buttonPins[i]);
            } else if (!newPressed && buttons[i].pressed) {
                // Button just released
                buttons[i].pressed = false;
                buttons[i].held = false;
                buttons[i].releasedEdge = true;
                ESP_LOGD(TAG, "Released %s (GPIO%d)", buttonName(static_cast<ButtonIndex>(i)), buttonPins[i]);
            } else if (newPressed && buttons[i].pressed) {
                // Button is being held
                if (!buttons[i].held && (currentTime - buttons[i].pressTime) > holdThreshold) {
                    buttons[i].held = true;
                }
            }
        }
    }
}

bool InputManager::isPressed(ButtonIndex btn) {
    if (btn >= BTN_COUNT) return false;
    return buttons[btn].pressed;
}

bool InputManager::wasPressed(ButtonIndex btn) {
    if (btn >= BTN_COUNT) return false;
    return buttons[btn].pressedEdge || (buttons[btn].pressed && (millis() - buttons[btn].pressTime) < kPressedEdgeWindowMs);
}

bool InputManager::wasReleased(ButtonIndex btn) {
    if (btn >= BTN_COUNT) return false;
    return buttons[btn].releasedEdge;
}

bool InputManager::isHeld(ButtonIndex btn) {
    if (btn >= BTN_COUNT) return false;
    return buttons[btn].held;
}

ButtonEvent InputManager::getButtonEvent(ButtonIndex btn) {
    if (btn >= BTN_COUNT) return BTN_NONE;
    
    if (wasPressed(btn)) return BTN_PRESSED;
    if (wasReleased(btn)) return BTN_RELEASED;
    if (isHeld(btn)) return BTN_HELD;
    
    return BTN_NONE;
}

void InputManager::clearButtonEvents(ButtonIndex btn) {
    if (btn >= BTN_COUNT) return;
    buttons[btn].pressedEdge = false;
    buttons[btn].releasedEdge = false;
}

void InputManager::clearAllButtonEvents() {
    for (int i = 0; i < BTN_COUNT; i++) {
        clearButtonEvents((ButtonIndex)i);
    }
}

void InputManager::clearButton(ButtonIndex btn) {
    clearButtonEvents(btn);
}

void InputManager::clearAllButtons() {
    clearAllButtonEvents();
}

bool InputManager::anyButtonPressed() {
    for (int i = 0; i < BTN_COUNT; i++) {
        if (buttons[i].pressed) {
            return true;
        }
    }
    return false;
}
