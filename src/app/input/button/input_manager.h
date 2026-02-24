#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>
#include <app_config.h>

enum ButtonIndex {
    BTN_UP = 0,
    BTN_DOWN = 1,
    BTN_SELECT = 2,
    BTN_BACK = 3,
    BTN_COUNT = 4,
};

enum ButtonEvent {
    BTN_NONE,
    BTN_PRESSED,
    BTN_RELEASED,
    BTN_HELD
};

struct Button {
    bool pressed;
    bool lastState;
    unsigned long lastPress;
    unsigned long pressTime;
    bool held;
    bool pressedEdge;
    bool releasedEdge;
};

class InputManager {
private:
    Button buttons[BTN_COUNT];
    int buttonPins[BTN_COUNT];
    unsigned long holdThreshold;
    
public:
    InputManager();
    void init();
    void update();
    
    // Button state queries
    bool isPressed(ButtonIndex btn);
    bool wasPressed(ButtonIndex btn);
    bool wasReleased(ButtonIndex btn);
    bool isHeld(ButtonIndex btn);
    ButtonEvent getButtonEvent(ButtonIndex btn);
    
    // Clear button states (useful after handling events)
    void clearButtonEvents(ButtonIndex btn);
    void clearAllButtonEvents();
    void clearButton(ButtonIndex btn);
    void clearAllButtons();
    
    // Check if any button is pressed
    bool anyButtonPressed();
    
    // Configuration
    void setHoldThreshold(unsigned long threshold) { holdThreshold = threshold; }

private:
    static constexpr unsigned long kDebounceDelayMs = 50;
    static constexpr unsigned long kPressedEdgeWindowMs = 50;
};

#endif // INPUT_MANAGER_H
