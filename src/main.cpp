// THUMBL-P: TemperatureHUmidityMotionBuzzerLight-Pressure

#define AP_NAME "BDIot"
#define FW_NAME "THUMBL-P"
#define FW_VERSION "0.0.6b"

#include <map>
#include <Homie.h>

#include "NonBlockingRtttl.h"

// Nodes libs
#include <BME280Node.hpp>
#include <SwitchNode.hpp>
#include <BinarySensorNode.hpp>
#include <ButtonNode.hpp>

#include "GL5528Node.h"

/**
 * TODO:
 * * create a PR for Homie prj and add a constructor overload to follow doc and simplify buzzerNode instance:
 *   HomieNode(const char* id, const char* name, const char* type, const HomieInternals::NodeInputHandler& nodeInputHandler = [](const HomieRange& range, const String& property, const String& value) { return false; });
 */

#ifndef SERIAL_SPEED
#define SERIAL_SPEED 115200
#endif


// For read ESP8266 VCC voltage
//ADC_MODE(ADC_VCC);
#define PIN_LED 2
#define PIN_BME280_SDA 4
#define PIN_BME280_SCL 5
#define PIN_PIR 12
#define PIN_BUZZER 13
#define PIN_BUTTON 14
#define PIN_LIGHT 15
#define BME280_I2C_ADDRESS 0x76
#define SETTING_LIGHT_MOTION_TIMEOUT 20
#define SETTING_LIGHT_MOTION_MAX_LUX 10.0

// Melodies preset
std::map<const String, const char *> rtttlMelodies = {
        {"two-short", ":d=4,o=5,b=100:16e6,16e6"},
        {"one-long",  ":d=1,o=5,b=100:e6"},
        {"siren",     ":d=8,o=5,b=100:d,e,d,e,d,e,d,e"},
        {"scale-up",  ":d=32,o=5,b=100:c,c#,d#,e,f#,g#,a#,b"},
        {"tetris",    ":d=4,o=5,b=160:e6,8b,8c6,8d6,16e6,16d6,8c6,8b,a,8a,8c6,e6,8d6,8c6,b,8b,8c6,d6,e6,c6,a,2a,8p,d6,8f6,a6,8g6,8f6,e6,8e6,8c6,e6,8d6,8c6,b,8b,8c6,d6,e6,c6,a,a"}
};

// Device custom settings
HomieSetting<long> settingLightOnMotionTimeout("lightOnMotionTimeout",
                                               "Light switch-off delay (seconds) after a motion detection [0, " TOSTRING(LONG_MAX) "] Default = " TOSTRING(SETTING_LIGHT_MOTION_TIMEOUT));
HomieSetting<double> settingLightOnMotionMaxLux("lightOnMotionMaxLux",
                                                "Max luminance (lux) to switch-on light on a motion detection [0, 100] Default = " TOSTRING(SETTING_LIGHT_MOTION_MAX_LUX));

// Forward declaration
void homieLoopHandler();

bool buzzerHandler(const HomieRange &range, const String &property, const String &value);

// Nodes instances
SwitchNode ledNode("light", "Led light", PIN_LIGHT);
BME280Node bme280Node("bme280", "BME280", BME280_I2C_ADDRESS);
GL5528Node photoresistorNode("luminance", "Luminance", 1000, 0.80f);
BinarySensorNode pirNode("motion", "Motion detector", PIN_PIR, INPUT, 5, HIGH);
ButtonNode buttonNode("button", "Button", PIN_BUTTON, INPUT_PULLUP, 20, LOW, 3, 1000);
// note: buzzerNode is implemented using Homie "classic method" (ie no custom node class)
HomieNode buzzerNode("buzzer", "Rtttl buzzer", "Sound", false, 0, 0, buzzerHandler);


/* HANDLERS / CALLBACKS */

// more events on: https://homieiot.github.io/homie-esp8266/docs/3.0.1/advanced-usage/events/
void onHomieEvent(const HomieEvent &event) {
    switch (event.type) {
        case HomieEventType::MQTT_READY:

            rtttl::begin(PIN_BUZZER, rtttlMelodies.find("scale-up")->second);
            break;

#ifdef DEBUG
            case HomieEventType::SENDING_STATISTICS: {

                // TODO: create a PR for Homie prj and add to Statistics:
                //  https://github.com/homieiot/homie-esp8266/blob/9cd83972f27b394eab8a5e3e2baef20eea1b5408/src/Homie/Boot/BootNormal.cpp#L175
                size_t baseTopicLength = strlen(Homie.getConfiguration().mqtt.baseTopic) + strlen(Homie.getConfiguration().deviceId);
                size_t longestSubtopicLength = 31 + 1;

                std::unique_ptr<char[]> freeHeapTopic = std::unique_ptr<char[]>(new char[baseTopicLength + longestSubtopicLength]);
                strcpy(freeHeapTopic.get(), Homie.getConfiguration().mqtt.baseTopic);
                strcat(freeHeapTopic.get(), Homie.getConfiguration().deviceId);
                strcat_P(freeHeapTopic.get(), PSTR("/$stats/free-heap"));

                char freeHeapStr[20];
                uint32_t freeHeap = ESP.getFreeHeap();
                utoa(freeHeap, freeHeapStr, 10);

                Homie.getLogger() << F("  • FreeHeap: ") << freeHeapStr << F("b") << endl;
                Homie.getMqttClient().publish(freeHeapTopic.get(), 1, true, freeHeapStr);

                break;
            }
#endif

        default:
            break;
    }
}

bool broadcastHandler(const String &topic, const String &value) {
    Homie.getLogger() << "Received broadcast " << topic << ": " << value << endl;
    return true;
}

bool globalInputHandler(const HomieNode &node, const HomieRange &range, const String &property, const String &value) {
    Homie.getLogger() << "Received/published an input msg on node " << node.getId() << ": " << property << " = " << value << endl;
    return false;
}

bool buzzerHandler(const HomieRange &range, const String &property, const String &value) {

    if (property == "play") {

        if (value.charAt(0) != '{') {
            // Melody name (between predefined ones)

            auto melody = rtttlMelodies.find(value.c_str());
            if (melody == rtttlMelodies.end()) { return true; }

            rtttl::begin(PIN_BUZZER, melody->second);
        } else {
            // Json payload
            // allow custom melody (rtttl format), loopCount and loopGap
            StaticJsonDocument<256> doc;

            deserializeJson(doc, value);

            JsonObjectConst json = doc.as<JsonObject>();

            const char *rtttl;

            if (json.containsKey(F("preset"))) {

                auto melodyIterator = rtttlMelodies.find(json[F("preset")]);
                if (melodyIterator == rtttlMelodies.end()) { return true; }

                rtttl = melodyIterator->second;

            } else if (json.containsKey(F("rtttl"))) {
                rtttl = json[F("rtttl")];

            } else {
                return true;
            }

            uint8_t loopCount = json[F("loop-count")] | 1;
            uint16_t loopGap = json[F("loop-gap")] | 0;

            rtttl::begin(PIN_BUZZER, rtttl, loopCount, loopGap);
        }

        return true;

    } else if (property == "stop") {

        if (value != "true") return true;

        rtttl::stop();

        return true;
    }

    return false;
}

bool buttonHandler(const ButtonEvent &event) {

    // TODO: remove
    if (event.type != ButtonEventType::HOLD) {

        Homie.getLogger() << "ButtonNode - " << "event n°" << static_cast<int>(event.type)
                          << " pressCount: " << event.pressCount
                          << " duration.current: " << event.duration.current
                          << " duration.previous: " << event.duration.previous
                          << endl;
    }

    switch (event.type) {
        case ButtonEventType::HOLD:
            if (event.duration.current >= 2000) rtttl::stop();
            break;
        case ButtonEventType::MULTI_PRESS_COUNT:
        case ButtonEventType::MULTI_PRESS_INTERVAL:
            if (event.pressCount > 1) rtttl::begin(PIN_BUZZER, rtttlMelodies.find("tetris")->second, event.pressCount, 2000);
            break;
        default:
            break;
    }

    return true;
}

bool motionHandler(bool state) {

    auto maxLux = settingLightOnMotionMaxLux.get();
    auto lightTimeout = settingLightOnMotionTimeout.get();

    Homie.getLogger() << "Motion " << state
                      // typecasts overloading (see RetentionVar impl.)
                      << " with " << (float) photoresistorNode << " lux (max " << maxLux << ')'
                      << endl;

    // Switch on light on motion and darkly
    if (state && photoresistorNode.value() < maxLux) {

        ledNode.stopTimeout();
        ledNode.setState(true);

    } else if (ledNode.getState()) {

        // Timeout start only on false, because motion stay true if continuously moving ahead it
        // ie. moving time > lightTimeout => light switch off while moving
        ledNode.setTimeout(lightTimeout);
    }

    return true;
}

bool photoresistorHandler(float lux) {
    Homie.getLogger() << "Luminosity: " << lux << " lux" << endl;
    return true;
}

void setup() {
    Serial.begin(SERIAL_SPEED);

    // Initializes I2C for BME280Node sensor
    // use default value. Uncomment and change as need
    //Wire.begin(PIN_BME280_SDA, PIN_BME280_SCL);
    pinMode(PIN_BUZZER, OUTPUT);

    // HOMIE SETUP
    Homie_setBrand(AP_NAME);                    // Brand in fw, mqtt client name and AP name in configuration mode
    Homie_setFirmware(FW_NAME, FW_VERSION);     // Node name and fw version (for OTA stuff)

    // Device custom settings: default and validation
    settingLightOnMotionTimeout.setDefaultValue(SETTING_LIGHT_MOTION_TIMEOUT).setValidator([](long candidate) {
        return 0 <= candidate && candidate <= LONG_MAX;
    });

    settingLightOnMotionMaxLux.setDefaultValue(SETTING_LIGHT_MOTION_MAX_LUX).setValidator([](long candidate) {
        return 0 <= candidate && candidate <= 100;
    });


    // Config ledNode for notify
    // * configuration mode (stay on)
    // * normal mode:
    //  * connecting to Wi-Fi (slowly blink)
    //  * connecting to MQTT (faster blink)
    //  * connected (stay off)
    //Homie.disableLedFeedback();
    //Homie.setLedPin(PIN_LED, HIGH);

    //Homie.disableLogging();
    // pressing for 5 seconds the FLASH (GPIO0) button (default behaviour)
    //Homie.disableResetTrigger();

    //Homie.setSetupFunction();
    Homie.setLoopFunction(homieLoopHandler);
    Homie.onEvent(onHomieEvent);
    Homie.setBroadcastHandler(broadcastHandler);
    Homie.setGlobalInputHandler(globalInputHandler);

    // NODES SETUP
    //ledNode.setOnSetFunc(onLightChange);
    //ledNode.getProperty("on")->settable(lightOnHandler);

    // call pirNode.loop() even when not connected (but still in normal mode)
    // i.e. some device features must works without connection
    pirNode.setRunLoopDisconnected(true);
    pirNode.setOnChangeFunc(motionHandler);

    photoresistorNode.setRunLoopDisconnected(true);
    photoresistorNode.setOnChangeFunc(photoresistorHandler);

    buttonNode.setRunLoopDisconnected(true);
    buttonNode.setOnChangeFunc(buttonHandler);

    // Buzzer MQTT topic/proprieties configuration
    buzzerNode.advertise("play").setDatatype("string").setFormat("json").settable();
    buzzerNode.advertise("stop").setDatatype("boolean").settable();
    buzzerNode.advertise("is-playing").setDatatype("boolean");

    Homie.setup();
}

void loop() {
    Homie.loop();

    // TODO: in configuration mode call isConfigured(), spam message "/homie/config.json doesn't exist", create a PR with the patch Homie.cpp.patch (still to very if it works)
    if (Homie.isConfigured()) {
        if (rtttl::isPlaying()) rtttl::play();
    }

    // Uncomment for ssl in case of instability (ie disconnection)
    // https://github.com/homieiot/homie-esp8266/issues/640
    // try delay(100); or yield();
}

// Homie loop func continuously called in Normal mode and connected
void homieLoopHandler() {

    // Buzzer MQTT topic "/is-playing" managing
    static bool lastIsPlaying = false;

    if (rtttl::isPlaying()) {
        // note: use support var lastIsPlaying because getProperty() implementation is a loop
        if (!lastIsPlaying) {
            buzzerNode.setProperty("is-playing").send(F("true"));
            lastIsPlaying = true;
        }

    } else if (lastIsPlaying) {
        buzzerNode.setProperty("is-playing").send(F("false"));
        lastIsPlaying = false;
    }
}