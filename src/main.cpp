/**
 *  @author Paulo Sérgio do Nascimento
 *  @date   2020-04-15
 *  @name   main
 *
 **/

#include <Arduino.h>
#include <avr/sleep.h>
#include <TinyDHT.h>
#include <VirtualWire.h>
#include <avr/interrupt.h>

#define BOARDS_POWER_ON_PIN 3 // Pin to power on the boards
#define OUTPUT_RF_433_PIN 0   // RF433 output pin 0
#define SOIL_ANALOG_PIN A1    // Soil measurement pin
#define BATTERY_ANALOG_PIN A2 // Battery measurement pin
#define DHT11_COM_PIN 1       // DHT sensor pin 1
#define DHTTYPE DHT11         // DHT sensor type
#define TX_SPEED 4000         // RF433 transmission speed 4000 bit/seg
/* #define BATTERY "battery"   // Battery measurement topic
#define SOIL_HUMIDITY "soil_humidity" // Soil humidity measurement topic
#define TEMPERATURE "temperature"   // Temperature measurement topic
#define HUMIDITY "humidity"         // Humidity measurement topic
#define TIME_TOPIC "time"           // Time topic */

// Routines to set and claer bits (used in the sleep code)
#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

// ------------------- STRUCTS -------------------
typedef struct message
{
    byte code;
    float value;
} Message;

// ------------------- CONSTANTS -------------------
const uint8_t SAMPLING_RATE = 10; // Sampling rate

// ------------------- VARIABLES -------------------
float humidity = 0;         // Variable to store the humidity
float temperature = 0;      // Variable to store the temperature
volatile boolean f_wdt = 1; // Variables for the Sleep/power down mode
Message messages[5];        // Array of messages

// ------------------- OBJECTS -------------------
DHT dht(DHT11_COM_PIN, DHTTYPE);

// ------------------- FUNCTIONS -------------------
void config_tx();                     // Configure the RF433 transmitter
void battery_measurement();           // Measure the battery voltage
void soil_humidity_measurement();     // Measure the soil humidity
void temp_humidity_measurement();     // Measure the temperature and humidity
void send_messages();                 // Send a message
float measurement(const uint8_t pin); // Measure the value of a pin
void adc_enable();                    // Enable the ADC
void adc_disable();                   // Disable the ADC
void enter_deep_sleep();              // Enter deep sleep mode
void setup_watchdog(int ii);          // Setup the watchdog
void measure_time();                  // Send the time
void setup_messages();                // Setup the messages

// ------------------- SETUP -------------------
void setup()
{
    pinMode(SOIL_ANALOG_PIN, INPUT);
    pinMode(BATTERY_ANALOG_PIN, INPUT);
    pinMode(BOARDS_POWER_ON_PIN, OUTPUT);
    digitalWrite(BOARDS_POWER_ON_PIN, HIGH);
    setup_messages();
    dht.begin();
    config_tx();
    delay(50);
    setup_watchdog(9);
}

// ------------------- LOOP -------------------
void loop()
{
    if (f_wdt == 1)
    {
        f_wdt = 0;
        delay(2500);
        battery_measurement();
        soil_humidity_measurement();
        temp_humidity_measurement();
        measure_time();
        send_messages();
        digitalWrite(BOARDS_POWER_ON_PIN, LOW);
        enter_deep_sleep();
        digitalWrite(BOARDS_POWER_ON_PIN, HIGH);
    }
}

// Watchdog Interrupt Service / is executed when watchdog timed out
ISR(WDT_vect)
{
    f_wdt = 1;
}

// ------------------- FUNCTIONS ------------------

// 0=16ms, 1=32ms,2=64ms,3=128ms,4=250ms,5=500ms
// 6=1 sec,7=2 sec, 8=4 sec, 9= 8sec
void setup_watchdog(int ii)
{

    byte bb;

    if (ii > 9)
        ii = 9;

    bb = ii & 7;
    if (ii > 7)
        bb |= (1 << 5);

    bb |= (1 << WDCE);

    MCUSR &= ~(1 << WDRF);
    // start timed sequence
    WDTCR |= (1 << WDCE) | (1 << WDE);
    // set new watchdog timeout value
    WDTCR = bb;
    WDTCR |= _BV(WDIE);
}

/**
 * Deep Sleep
 *
 */
void enter_deep_sleep()
{
    adc_disable();
    for (uint8_t i = 0; i < 9; i++) // +/- 72 seconds
    {
        cbi(ADCSRA, ADEN); // switch Analog to Digitalconverter OFF
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        sleep_enable();
        sleep_mode();
        sleep_disable();
        sbi(ADCSRA, ADEN); // switch Analog to Digitalconverter ON
    }
    adc_enable();
}

/**
 * ADC disable
 */
void adc_disable()
{
    ADCSRA &= ~(1 << ADEN);
}

/**
 * ADC enable
 */
void adc_enable()
{
    ADCSRA |= (1 << ADEN);
}

/**
 * Configure the transmitter
 */
void config_tx()
{
    vw_set_tx_pin(OUTPUT_RF_433_PIN);
    vw_set_ptt_inverted(true);
    vw_setup(TX_SPEED);
}

/**
 * Measure
 */
float measurement(const uint8_t pin)
{
    float _measurement = 0;
    for (size_t i = 0; i < SAMPLING_RATE; i++)
    {
        _measurement += analogRead(pin);
        delay(15);
    }
    return _measurement / SAMPLING_RATE;
}

/**
 * Send a message
 * @param message the message to send
 */
void send_messages()
{
    vw_send((uint8_t *)&messages, sizeof(messages));
    vw_wait_tx();
}

/**
 * Measure the soil humidity
 */
void soil_humidity_measurement()
{
    float result = (measurement(SOIL_ANALOG_PIN) / 1023.0F) * 100.0F;
    messages[1].value = 100.0F - result;
}

/**
 * Measure the temperature and humidity
 */
void temp_humidity_measurement()
{
    messages[2].value = dht.readTemperature(0);
    messages[3].value = dht.readHumidity();
}

/**
 * Measure the temperature
 */
void battery_measurement()
{
    analogReference(INTERNAL);
    delay(15);
    messages[0].value = (measurement(BATTERY_ANALOG_PIN) / 1023.0F) * 5.79F; // 5.79 empirically determined
    analogReference(DEFAULT);
}

/**
 * Send the time
 */
void measure_time()
{
    messages[4].value = millis() / 1000;
}

/**
 * Setup messages
 */
void setup_messages()
{
    messages[0].code = 0;
    // strlcpy(messages[0].msg, BATTERY, sizeof(BATTERY));
    messages[0].value = -1.0;

    messages[1].code = 1;
    // strlcpy(messages[1].msg, SOIL_HUMIDITY, sizeof(SOIL_HUMIDITY));
    messages[1].value = -1.0;

    messages[2].code = 2;
    // strlcpy(messages[2].msg, TEMPERATURE, sizeof(TEMPERATURE));
    messages[2].value = -1.0;

    messages[3].code = 3;
    // strlcpy(messages[3].msg, HUMIDITY, sizeof(HUMIDITY));
    messages[3].value = -1.0;

    messages[4].code = 4;
    // strlcpy(messages[4].msg, TIME_TOPIC, sizeof(TIME_TOPIC));
    messages[4].value = -1.0;
}