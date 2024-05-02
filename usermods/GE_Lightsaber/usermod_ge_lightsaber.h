#pragma once

#include "wled.h"

class LightsaberUsermod : public Usermod
{
private:
    bool lightsaberOn = false;
    bool lightsaberWasOn = false;
    const int HILT_DATA_PIN = 4;

// stock blade colors; these values are based on PWM duty cycles measured from a stock blade controller
#define RGB_BLADE_WHITE LED_RGB(102, 102, 102)
#define RGB_BLADE_RED LED_RGB(255, 0, 0)
#define RGB_BLADE_ORANGE LED_RGB(231, 77, 0)
#define RGB_BLADE_YELLOW LED_RGB(154, 154, 0)
#define RGB_BLADE_GREEN LED_RGB(0, 255, 0)
#define RGB_BLADE_CYAN LED_RGB(0, 154, 154)
#define RGB_BLADE_BLUE LED_RGB(0, 0, 255)
#define RGB_BLADE_PURPLE LED_RGB(154, 0, 154)
#define RGB_BLADE_DARK_PURPLE LED_RGB(26, 0, 13)
#define RGB_BLADE_CLASH_YELLOW LED_RGB(255, 255, 0)
#define RGB_BLADE_CLASH_ORANGE LED_RGB(255, 64, 0)
#define RGB_BLADE_CLASH_WHITE LED_RGB(128, 128, 128)
#define RGB_BLADE_OFF LED_RGB(0, 0, 0)

#define VALID_BIT_CUTOFF 4000       // any HIGH period on the data line longer than this value, in microseconds, is considered an invalid bit of data and causes a reset of the data capture
#define VALID_BIT_ONE 1600          // any HIGH period longer than this value, in microseconds, but less than VALID_BIT_CUTOFF is treated as a valid 1 bit
#define SLEEP_AFTER 6000000         // how many ms to wait, after turning off, before going to sleep to conserve power
#define VALID_BIT_CUTOFF 4000       // any HIGH period on the data line longer than this value, in microseconds, is considered an invalid bit of data and causes a reset of the data capture
#define VALID_BIT_ONE 1600          // any HIGH period longer than this value, in microseconds, but less than VALID_BIT_CUTOFF is treated as a valid 1 bit
                                    // any HIGH period shorter than this value, in microseconds, is treated as a valid 0 bit
                                    // if blade is not registering commands correctly, this value likely needs to be tweaked
                                    // typically a 1 bit is about 2000uS (Legacy) or 2400uS (Savi) long and a 0 is 1200uS long.
                                    // this value is set between 2000 and 1200 to accomadate delays in timing/processing
#define COLOR_MODE_CHANGE_TIME 1500 // if a blade is turned off then on again within this amount of time, then change to the next color mode
#define COLOR_WHEEL_PAUSE_TIME 2000 // how long to hold a color before moving to the next color
#define COLOR_WHEEL_CYCLE_STEP 16   // how many steps to jump when calculating the next color in the color cycle; a power of 2 is recommended

// set WLED preset IDs for these blade states (TODO add settings pages for this later once it's actually working)
#define BLADE_UNINITIALIZED_PRESET 150
#define BLADE_OFF_PRESET 151
#define BLADE_IGNITING_PRESET 152
#define BLADE_ON_PRESET 153
#define BLADE_IDLE_PRESET 154
#define BLADE_CLASH_PRESET 155
#define BLADE_EXTINGUISHING_PRESET 156
#define BLADE_REFRESH_PRESET 157
#define BLADE_FLICKER_LOW_PRESET 158
#define BLADE_FLICKER_HIGH_PRESET 159

    // blade states
    typedef enum
    {
        BLADE_UNINITIALIZED,
        BLADE_OFF,
        BLADE_IGNITING,
        BLADE_ON,
        BLADE_IDLE,
        BLADE_CLASH,
        BLADE_EXTINGUISHING,
        BLADE_REFRESH,
        BLADE_FLICKER_LOW,
        BLADE_FLICKER_HIGH
    } state_t;
    // color mode
    typedef enum
    {
        COLOR_MODE_STOCK,
        COLOR_MODE_WHEEL_CYCLE,
        COLOR_MODE_WHEEL_HOLD
    } color_mode_t;

    // blade properties template
    typedef struct
    {
        state_t state;
        uint8_t cmd;
        color_mode_t color_mode;
    } blade_t;

    // global blade properties object
    blade_t blade;

    // global variable where decoded hilt command is stored
    // it has the volatile keyword because it will be updated from within an interrupt service routine
    volatile uint8_t hilt_cmd = 0;

public:
    void blade_manager()
    {
        static uint32_t next_step = 0;
        static uint32_t animate_step = 0;
        static uint32_t last_extinguish = 0;
        static state_t last_state = BLADE_UNINITIALIZED;
        static uint8_t wheel_index = 0;
        uint16_t target;

        //
        // ** BLADE MANAGER STAGE ONE : NEW STATE INITIALIZATION **
        //
        // The blade has changed states. In this section of code any initialization needed for the new
        // blade state is handled.
        //
        if (last_state != blade.state)
        {
            last_state = blade.state;

            // do not allow a refresh, on, or idle state change to disrupt any potential ongoing effects
            switch (blade.state)
            {
            case BLADE_REFRESH:
            case BLADE_ON:
            case BLADE_IDLE:
                break;
            default:
                next_step = 0;
                animate_step = 0;
                break;
            }

            switch (blade.state)
            {

            // the blade is off. disable any running animations and shut the LEDs off
            case BLADE_OFF:

                // set the point when the blade controller should go to sleep
                next_step = millis() + SLEEP_AFTER;

                // Trigger WLED preset to turn the LEDs off
                applyPreset(BLADE_OFF_PRESET);

                break;

            // the blade is powering on
            case BLADE_IGNITING:

                // switch color modes if blade was off for less than COLOR_MODE_CHANGE_TIME
                if (last_extinguish > 0 && (millis() - last_extinguish) < COLOR_MODE_CHANGE_TIME)
                {

                    // the color mode we move to next is based on the current color mode
                    switch (blade.color_mode)
                    {

                    case COLOR_MODE_STOCK:

                        blade.color_mode = COLOR_MODE_WHEEL_CYCLE;
                        break;

                    case COLOR_MODE_WHEEL_CYCLE:

                        blade.color_mode = COLOR_MODE_WHEEL_HOLD;
                        break;

                    default:

                        blade.color_mode = COLOR_MODE_STOCK;
                        break;
                    }
                }

                // set the color and clash color of the blade based on the current color mode
                switch (blade.color_mode)
                {

                // wheel color is based on wheel_index
                case COLOR_MODE_WHEEL_CYCLE:
                case COLOR_MODE_WHEEL_HOLD:
                    // TODO decide if we want to do anything here
                    break;

                // in all other instances, use the stock blade color
                default:
                    // TODO decide if we want to do anything here
                    break;
                }

                // delay ignition based on whatever value is stored in the lightsaber's properties
                next_step = millis();
                break;

            // BLADE_ON is when the blade has just finished igniting; perhaps there's something we'll want to do only
            // under that situation, which is why BLADE_ON and BLADE_IDLE are separate things
            case BLADE_ON:
                blade.state = BLADE_IDLE;
                applyPreset(BLADE_ON_PRESET);

                break;

            // blade is at idle
            case BLADE_IDLE:
                applyPreset(BLADE_IDLE_PRESET);
                // some color modes may want to do something while the blade is idling
                switch (blade.color_mode)
                {

                // in color wheel cycle mode, cycle through colors in the wheel every COLOR_WHEEL_PAUSE_TIME milliseconds
                //
                // at this point, it's possible we're entering an IDLE state after a blade refresh, in which case we don't
                // want to touch next_step; only set next_step if it has a value less than the current time in ms
                case COLOR_MODE_WHEEL_CYCLE:
                    if (next_step < millis())
                    {
                        next_step = millis() + COLOR_WHEEL_PAUSE_TIME;
                    }
                    break;
                }
                break;

            // a clash command has been sent; this happens when the blade hits something or the hilt stops suddenly
            case BLADE_CLASH:
                applyPreset(BLADE_CLASH_PRESET);
                // wait 40 milliseconds and then change the blade color back to normal
                next_step = millis() + 40;
                break;

            // the blade is turning off
            case BLADE_EXTINGUISHING:
                last_extinguish = millis();
                applyPreset(BLADE_EXTINGUISHING_PRESET);
                break;

            // every second or so the hilt sends this command to the blade. this is done to keep blade the correct color in the event
            // it should wiggle in its socket and momentarily lose connection and reset or a corrupted data command sets the blade
            // to a color other than what it should be
            case BLADE_REFRESH:

                // change state to on
                blade.state = BLADE_ON;
                break;

            // low brightness flicker command; set the blade to some brightness level between 0 and 50% based on the value supplied
            case BLADE_FLICKER_LOW:
                // not sure I care about handling this?
                next_step = millis() + 40;
                break;

            // high brightness flicker command; set the blade to some brightness level between 50 and 100% based on the value supplied
            case BLADE_FLICKER_HIGH:
                // not sure I care about handling this?
                next_step = millis() + 40;
                break;

            default:
                break;
            }
        }

        //
        // ** BLADE MANAGER STAGE TWO : ONGOING ACTION **
        //
        // If next_step has a value greater than 0 then some animation is happening.
        //
        // If next_step is less than or equal to the current time in milliseconds then it's time to execute the next step
        // of action (animation, color change, etc.)
        //
        if (next_step > 0 && next_step <= millis())
        {
            switch (blade.state)
            {

            // animate the blade igniting by turning on 1 LED at a time
            case BLADE_IGNITING:
                // TODO trigger blade_igniting preset in WLED
                break;

            // second part of the clash animation; set the blade back to its normal color
            case BLADE_CLASH:
                // TODO trigger a preset here OR a command to the LEDS to flash
                break;

            // animate the blade extinguishing by turning off 1 LED at a time
            case BLADE_EXTINGUISHING:

                // TODO trigger preset ID for blade_extinguising

                break;

            // second part of the flicker, reduce brightness by 20% (not sure these are needed anymore in WLED)
            case BLADE_FLICKER_LOW:
            case BLADE_FLICKER_HIGH:
                // TODO: code to adjust brightness in WLED
                next_step = 0;
                blade.state = BLADE_IDLE;
                break;

            case BLADE_OFF:
                // blade has been off for SLEEP_AFTER milliseconds. go to sleep to conserve power
                // really not sure we need to do anything here? Blade extinguish should be able to handle it now.

                // after waking up, reset sleep timer in case blade never leaves the off state
                next_step = millis() + SLEEP_AFTER;
                break;

            // blade is at idle
            case BLADE_IDLE:
            // do things?
            default:
                break;
            }
        }
    }

    void process_command()
    {

// DEBUG: display decoded command to serial monitor

        // identify command, set blade state and colors (if needed)
        switch (blade.cmd & 0xF0)
        {

        case 0x20: // savi's ignite
            blade.state = BLADE_IGNITING;
            break;

        case 0x30: // legacy ignite
            blade.state = BLADE_IGNITING;
            break;

        case 0xA0: // savi's set color

            // only perform a blade refresh if the blade is in an idle state or if it's off (to force it on after a missed ignite)
            if (blade.state == BLADE_IDLE || blade.state == BLADE_OFF)
            {
                blade.state = BLADE_REFRESH;
            }
            break;

        case 0xB0: // legacy set color

            // only perform a blade refresh if the blade is in an idle state
            if (blade.state == BLADE_IDLE || blade.state == BLADE_OFF)
            {
                blade.state = BLADE_REFRESH;
            }
            break;

        case 0x40: // savi's extinguish
        case 0x50: // legacy extinguish
            blade.state = BLADE_EXTINGUISHING;
            break;

        case 0x80: // off
        case 0x90: // off
        case 0xE0: // off
        case 0xF0: // off
            blade.state = BLADE_OFF;
            break;

        case 0xC0: // savi's clash
        case 0xD0: // legacy clash
            blade.state = BLADE_CLASH;
            break;

        case 0x60: // blade flicker low (0-50% brightness)
            blade.state = BLADE_FLICKER_LOW;
            break;

        case 0x70: // blade flicker high (53-100% brightness)
            blade.state = BLADE_FLICKER_HIGH;
            break;

        default:
            break;
        }
    }

    // read data from the hilt
    // data is sent in a series of pulses. each pulse represents a 1-bit value (0 or 1)
    // how long the pulse lasts will determine if it's a 1 or a 0
    // collect 8 pulses in a row in a single, 8-bit value and store that to the global hilt_cmd variable
    void read_cmd()
    {
        static uint8_t cmd = 0;
        static uint8_t bPos = 0;
        static uint32_t last_change = 0;
        uint32_t period = 0;

        // when transitioning from HIGH to LOW record the time the data line was HIGH
        // record the bit value that corresponds to the the length of time the data line was HIGH
        if (digitalRead(HILT_DATA_PIN) == LOW)
        {

            // determine the length of the the data line was high
            period = micros() - last_change;

            // if it was HIGH for less than 4ms treat it as a good bit value
            if (period < VALID_BIT_CUTOFF)
            {

                // shift the bits in the cmd variable left by 1 to make room for the new bit
                cmd <<= 1;

                // count that another bit has been received
                bPos++;

                // a HIGH period greater than 2ms can be treated as a 1, otherwise it is a 0
                // analysis of GE hilts shows that typically 1200-1600uS = 0, 2400-3000uS = 1
                // this value may need tweaking; see DEFINE near top of code
                if (period > VALID_BIT_ONE)
                {
                    cmd++;
                }

                // if more than 7 bits have been recorded (8 bits), we have a good 8-bit command.
                if (bPos > 7)
                {

                    // store the 8-bit command to a global variable which will be picked up by loop() function
                    hilt_cmd = cmd;

                    // reset bit collection
                    cmd = 0;
                    bPos = 0;
                }

                // anything longer than 4ms reset bit collection
            }
            else
            {
                cmd = 0;
                bPos = 0;
            }

            // just transitioned into LOW (ACTIVE) period, record the time when it started
        }
        else
        {
            last_change = micros();
        }
    }

    void setup()
    {
        // Initialize your lightsaber signal reading here

        // setup DATA pin for hilt
        pinMode(HILT_DATA_PIN, INPUT_PULLUP);

        // attachInterrupt(digitalPinToInterrupt(HILT_DATA_PIN), read_cmd, CHANGE);

        // start the blade in an OFF state
        blade.state = BLADE_OFF;
    }

    void loop()
    {
        // check for a command from the hilt
        if (hilt_cmd != 0)
        {

            // copy command to a local variable as hilt_cmd is a global, volatile
            // variable that could be overwritten at any second
            blade.cmd = hilt_cmd;

            // immediately set hilt_cmd to zero so we know we've processed it
            hilt_cmd = 0;

            // call function that processes the command received from the hilt
            process_command();
        }

        // handle blade animations, sleep/wake, react to state changes, etc.
        blade_manager();
    }

    void led_power_on()
    {
        // Apply preset for lightsaber turning on (put this in usermod config settings)
        // Example: setColorFromPreset(1); // Apply preset with ID 1
    }

    void led_power_off()
    {
        // Apply preset for lightsaber turning off (put this in usermod config settings)
        // Example: setColorFromPreset(2); // Apply preset with ID 2
    }

    // Add more methods for configuration, preset management, etc. as needed
};
