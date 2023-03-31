#include <Arduino.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <WiFi.h>
#include <Fsm.h>
#include <DHT.h>
#include <Time.h>
#include <JC_Button.h>
#include <DS3232RTC.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include "secrets.h"

SoftwareSerial softwareSerial(34, 35); // RX, TX

const char *SSID = WIFI_SSID;
const char *PASS = WIFI_PASSWORD;
const char *server = "mqtt3.thingspeak.com";
const char *channelID = CHANNEL_ID;
const char *mqttUserName = SECRET_MQTT_USERNAME;
const char *mqttPass = SECRET_MQTT_PASSWORD;
const char *clientID = SECRET_MQTT_CLIENT_ID;
WiFiClient client;
PubSubClient mqtt(client);

TaskHandle_t Task0;
TaskHandle_t Task1;

#define CONNECTION_TIMEOUT 20

#define BUTTON_PIN_LEFT 19
#define BUTTON_PIN_RIGHT 18
#define BUTTON_PIN_UP 5
#define BUTTON_PIN_DOWN 17
#define BUTTON_PIN_MENU_SELECT 16
#define BUTTON_PIN_BACK 4
#define SQW_PIN 23
#define ALARM_OUT 13

#define DHT_PIN 2
#define DHT_TYPE DHT22

#define DEBOUNCE_MS 20
#define PULLUP true
#define INVERT true
#define REPEAT_FIRST 1000
#define REPEAT_INCR 255

#define EEPROM_SIZE 5

LiquidCrystal_I2C LCD = LiquidCrystal_I2C(0x27, 16, 2);
DHT dht(DHT_PIN, DHT_TYPE);
DS3232RTC RTC;
unsigned int pm1 = 0;
unsigned int pm2_5 = 0;
unsigned int pm10 = 0;

/*
   Symbols
*/
byte BELL[8] = {
    0b00100,
    0b01110,
    0b01110,
    0b01110,
    0b01110,
    0b11111,
    0b00000,
    0b00100};

byte THERMOMETER[8] = {
    0b00100,
    0b01010,
    0b01010,
    0b01110,
    0b01110,
    0b11111,
    0b11111,
    0b01110};

byte CHAR_A[8] = {
    0b01110,
    0b11111,
    0b11011,
    0b11011,
    0b11111,
    0b11111,
    0b11011,
    0b11011};

byte CHAR_L[8] = {
    0b11000,
    0b11000,
    0b11000,
    0b11000,
    0b11000,
    0b11000,
    0b11111,
    0b11111};

byte CHAR_C[8] = {
    0b11111,
    0b11111,
    0b11000,
    0b11000,
    0b11000,
    0b11000,
    0b11111,
    0b11111};

byte CHAR_EXCL[8] = {
    0b00110,
    0b00110,
    0b00110,
    0b00110,
    0b00110,
    0b00000,
    0b00110,
    0b00110};

byte MENU_RIGHT_ARROW[8] = {
    0b10000,
    0b11000,
    0b11100,
    0b11110,
    0b11100,
    0b11000,
    0b10000,
    0b00000};

byte MENU_LEFT_ARROW[8] = {
    0b00001,
    0b00011,
    0b00111,
    0b01111,
    0b00111,
    0b00011,
    0b00001,
    0b00000};

/*
   States of FSM
*/
enum STATES
{
  MAIN,
  MENU_SET_ALARM,
  MENU_SET_TIME,
  MENU_SET_DATE,

  SET_HOUR,
  SET_MINUTE,

  SET_DAY,
  SET_MONTH,
  SET_YEAR,

  SET_ALARM_HOUR,
  SET_ALARM_MINUTE,
  SET_ALARM_ON_OFF,
  ALARM_TIME,
  // Otherwise, it times out after 5 seconds, discards the changes and returns to displaying the time
};

STATES state = MAIN;

enum BUTTONS
{
  IDLE,
  BUTTON_LEFT,
  BUTTON_RIGHT,
  BUTTON_UP,
  BUTTON_DOWN,
  BUTTON_OK,
  BUTTON_BACK,
};

BUTTONS button = IDLE;

typedef struct
{
  int hour;
  int minute;
  int second;
} time_comp;

typedef struct
{
  int day;
  int month;
  int year;
} date_comp;

typedef struct
{
  int hour;
  int minute;
  bool active;
} alarm_comp;

typedef struct sensorData
{
  int humidity = 0;
  int temperature = 0;
  int pm1_0 = 0;
  int pm2_5 = 0;
  int pm10_0 = 0;
};

volatile sensorData Datas;
SemaphoreHandle_t sendReadySemaphore;
hw_timer_t *timer = NULL;

time_comp time_value;
date_comp date_value;
alarm_comp alarm_value;

int8_t dow;
String day_of_week;

uint32_t blink_interval = 300;
uint32_t blink_previousMillis = 0;
bool blink_state = false;
bool long_press_button = false;
unsigned long rpt = REPEAT_FIRST;
volatile bool alarm_isr_was_called = false;

/*
   Transition callback functions on ENTER
*/
void on_main_enter();
void on_menu_set_time_enter();
void on_menu_set_date_enter();
void on_menu_set_alarm_enter();
void on_set_hour_enter();
void on_set_minute_enter();
void on_set_day_enter();
void on_set_month_enter();
void on_set_year_enter();
void on_set_alarm_hour_enter();
void on_set_alarm_minute_enter();
void on_set_alarm_on_off_enter();

/*
   Transition callback functions on STATE
*/
void main_on_state();
void menu_set_time_on_state();
void menu_set_date_on_state();
void menu_set_alarm_on_state();
void set_hour_on_state();
void set_minute_on_state();
void set_day_on_state();
void set_month_on_state();
void set_year_on_state();
void set_alarm_hour_on_state();
void set_alarm_minute_on_state();
void set_alarm_on_off_on_state();

/*
   Transition callback functions on EXIT
*/
void on_exit();
void on_cancel();

void get_time(time_comp *time_comp);
void set_time(time_comp *time_comp);
void on_time_set();
void display_time();
void get_date(date_comp *date_comp);
void set_date(date_comp *date_comp);
void on_date_set();
void display_date();
void get_alarm(alarm_comp *alarm_comp);
void set_alarm(alarm_comp *alarm_comp);
void on_alarm_set();

void display_pm();
void display_menu(String menu);
void display_temperature();
void check_buttons();
void transition(BUTTONS trigger);
byte dec2bcd(byte val);
byte bcd2dec(byte val);
void display_position(int digits);
void set_blink_state();
void reset_blink();
void increase(int &number, int max, int min);
void decrease(int &number, int max, int min);
void alarm_isr();
void beep();

/*
   Initialize push buttons
*/
Button buttonLeft(BUTTON_PIN_LEFT, DEBOUNCE_MS, PULLUP, INVERT);
Button buttonRight(BUTTON_PIN_RIGHT, DEBOUNCE_MS, PULLUP, INVERT);
Button buttonUp(BUTTON_PIN_UP, DEBOUNCE_MS, PULLUP, INVERT);
Button buttonDown(BUTTON_PIN_DOWN, DEBOUNCE_MS, PULLUP, INVERT);
Button buttonOK(BUTTON_PIN_MENU_SELECT, DEBOUNCE_MS, PULLUP, INVERT);
Button buttonBack(BUTTON_PIN_BACK, DEBOUNCE_MS, PULLUP, INVERT);

/*
   Initialize states of FSM
*/
State state_main(&on_main_enter, &main_on_state, &on_exit);
State state_menu_set_time(&on_menu_set_time_enter, &menu_set_time_on_state, &on_exit);
State state_menu_set_date(&on_menu_set_date_enter, &menu_set_date_on_state, &on_exit);
State state_menu_set_alarm(&on_menu_set_alarm_enter, &menu_set_alarm_on_state, &on_exit);
State state_set_hour(&on_set_hour_enter, &set_hour_on_state, &on_exit);
State state_set_minute(&on_set_minute_enter, &set_minute_on_state, &on_exit);
State state_set_day(&on_set_day_enter, &set_day_on_state, &on_exit);
State state_set_month(&on_set_month_enter, &set_month_on_state, &on_exit);
State state_set_year(&on_set_year_enter, &set_year_on_state, &on_exit);
State state_set_alarm_hour(&on_set_alarm_hour_enter, &set_alarm_hour_on_state, &on_exit);
State state_set_alarm_minute(&on_set_alarm_minute_enter, &set_alarm_minute_on_state, &on_exit);
State state_set_alarm_on_off(&on_set_alarm_on_off_enter, &set_alarm_on_off_on_state, &on_exit);

Fsm fsm(&state_main);

void fsm_add_transitions()
{
  // MAIN
  fsm.add_transition(&state_main, &state_menu_set_time, BUTTON_OK, NULL);

  // MENU_SET_TIME
  fsm.add_transition(&state_menu_set_time, &state_menu_set_date, BUTTON_RIGHT, NULL);
  fsm.add_transition(&state_menu_set_time, &state_set_hour, BUTTON_OK, NULL);
  fsm.add_transition(&state_menu_set_time, &state_main, BUTTON_BACK, NULL);
  fsm.add_timed_transition(&state_menu_set_time, &state_main, 6000, NULL);

  // MENU_SET_DATE
  fsm.add_transition(&state_menu_set_date, &state_menu_set_alarm, BUTTON_RIGHT, NULL);
  fsm.add_transition(&state_menu_set_date, &state_menu_set_time, BUTTON_LEFT, NULL);
  fsm.add_transition(&state_menu_set_date, &state_set_day, BUTTON_OK, NULL);
  fsm.add_transition(&state_menu_set_date, &state_main, BUTTON_BACK, NULL);

  // MENU_SET_ALARM
  fsm.add_transition(&state_menu_set_alarm, &state_menu_set_date, BUTTON_LEFT, NULL);
  fsm.add_transition(&state_menu_set_alarm, &state_main, BUTTON_BACK, NULL);
  fsm.add_transition(&state_menu_set_alarm, &state_set_alarm_hour, BUTTON_OK, NULL);

  // SET_HOUR
  fsm.add_transition(&state_set_hour, &state_set_minute, BUTTON_RIGHT, NULL);
  fsm.add_transition(&state_set_hour, &state_main, BUTTON_OK, &on_time_set);
  fsm.add_transition(&state_set_hour, &state_main, BUTTON_BACK, &on_cancel);

  // SET_MINUTE
  fsm.add_transition(&state_set_minute, &state_set_hour, BUTTON_LEFT, NULL);
  fsm.add_transition(&state_set_minute, &state_main, BUTTON_OK, &on_time_set);
  fsm.add_transition(&state_set_minute, &state_main, BUTTON_BACK, &on_cancel);

  // SET_DAY
  fsm.add_transition(&state_set_day, &state_set_month, BUTTON_RIGHT, NULL);
  fsm.add_transition(&state_set_day, &state_main, BUTTON_OK, &on_date_set);
  fsm.add_transition(&state_set_day, &state_main, BUTTON_BACK, &on_cancel);

  // SET_MONTH
  fsm.add_transition(&state_set_month, &state_set_day, BUTTON_LEFT, NULL);
  fsm.add_transition(&state_set_month, &state_set_year, BUTTON_RIGHT, NULL);
  fsm.add_transition(&state_set_month, &state_main, BUTTON_OK, &on_date_set);
  fsm.add_transition(&state_set_month, &state_main, BUTTON_BACK, &on_cancel);

  // SET_YEAR
  fsm.add_transition(&state_set_year, &state_set_month, BUTTON_LEFT, NULL);
  fsm.add_transition(&state_set_year, &state_main, BUTTON_OK, &on_date_set);
  fsm.add_transition(&state_set_year, &state_main, BUTTON_BACK, &on_cancel);

  // SET_ALARM_HOUR
  fsm.add_transition(&state_set_alarm_hour, &state_set_alarm_minute, BUTTON_RIGHT, NULL);
  fsm.add_transition(&state_set_alarm_hour, &state_main, BUTTON_OK, &on_alarm_set);
  fsm.add_transition(&state_set_alarm_hour, &state_main, BUTTON_BACK, &on_cancel);

  // SET_ALARM_MINUTE
  fsm.add_transition(&state_set_alarm_minute, &state_set_alarm_hour, BUTTON_LEFT, NULL);
  fsm.add_transition(&state_set_alarm_minute, &state_set_alarm_on_off, BUTTON_RIGHT, NULL);
  fsm.add_transition(&state_set_alarm_minute, &state_main, BUTTON_OK, &on_alarm_set);
  fsm.add_transition(&state_set_alarm_minute, &state_main, BUTTON_BACK, &on_cancel);

  // SET_ALARM_ON_OFF
  fsm.add_transition(&state_set_alarm_on_off, &state_set_alarm_minute, BUTTON_LEFT, NULL);
  fsm.add_transition(&state_set_alarm_on_off, &state_main, BUTTON_OK, &on_alarm_set);
  fsm.add_transition(&state_set_alarm_on_off, &state_main, BUTTON_BACK, &on_cancel);
}

void spinner()
{
  static int8_t counter = 0;
  const char *glyphs = "\xa1\xa5\xdb";
  LCD.setCursor(15, 1);
  LCD.print(glyphs[counter++]);
  if (counter == strlen(glyphs))
  {
    counter = 0;
  }
}

void connect_wifi()
{
  LCD.init();
  LCD.backlight();
  LCD.setCursor(0, 0);
  LCD.print("CONNECTING TO ");
  LCD.setCursor(0, 1);
  LCD.print("WiFi ");

  int timeout_counter = 0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(250);
    spinner();

    timeout_counter++;
    if (timeout_counter >= CONNECTION_TIMEOUT * 5)
    {
      ESP.restart();
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void create_symbols()
{
  LCD.createChar(1, BELL);
  LCD.createChar(2, MENU_LEFT_ARROW);
  LCD.createChar(3, MENU_RIGHT_ARROW);
  LCD.createChar(4, THERMOMETER);
  LCD.createChar(5, CHAR_A);
  LCD.createChar(6, CHAR_L);
  LCD.createChar(7, CHAR_C);
  LCD.createChar(8, CHAR_EXCL);
}

void IRAM_ATTR onTimer()
{
  xSemaphoreGiveFromISR(sendReadySemaphore, NULL);
}

void alarm_clock_task(void *parameter)
{
  for (;;)
  {
    fsm.run_machine();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void send_mqtt_task(void *parameter)
{
  for (;;)
  {
    if (xSemaphoreTake(sendReadySemaphore, portMAX_DELAY))
    {
      Serial.println("in T1");
      // Check if WiFi is connected
      if (WiFi.status() == WL_CONNECTED)
      {
        // Serial.println("WiFi still conneted");
        vTaskDelay(500 / portTICK_PERIOD_MS);

        // Check if MQTT Server is connected
        if (!mqtt.connected())
        {
          mqtt.connect(clientID, mqttUserName, mqttPass);
          if (!mqtt.connected())
          {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
            Serial.print(".");
          }
        }

        Serial.println("MQTT still conneted");
        Serial.println("MQTT Sending");
        // Reset Timer

        String dataString = "&field1=" + String(Datas.humidity) + "&field2=" + String(Datas.temperature) + "&field3=" + String(Datas.pm1_0) + "&field4=" + String(Datas.pm2_5) + "&field5=" + String(Datas.pm10_0);
        String topicString = "channels/" + String(channelID) + "/publish";
        Serial.println(dataString);
        mqtt.publish(topicString.c_str(), dataString.c_str());

        // Update Half seconds every 500 ms
        vTaskDelay(500 / portTICK_PERIOD_MS);
      }
      else
      {
        Serial.println("WiFi connecting");
        WiFi.mode(WIFI_STA);
        WiFi.begin(SSID, PASS);

        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_EVENT_MAX)
        {
        }
        if (WiFi.status() != WL_CONNECTED)
        {
          Serial.println("WiFi Failed");
          vTaskDelay(10000 / portTICK_PERIOD_MS);
          continue;
        }
      }
    }
  }
}

void setup()
{
  Wire.begin(SDA, SCL);
  Serial.begin(9600);
  EEPROM.begin(EEPROM_SIZE);
  softwareSerial.begin(9600);
  buttonLeft.begin();
  buttonRight.begin();
  buttonUp.begin();
  buttonDown.begin();
  buttonOK.begin();
  buttonBack.begin();
  dht.begin();

  create_symbols();
  connect_wifi();

  pinMode(ALARM_OUT, OUTPUT);
  pinMode(SQW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SQW_PIN), alarm_isr, FALLING);

  fsm_add_transitions();
  setSyncProvider(RTC.get);
  setSyncInterval(5);
  RTC.squareWave(DS3232RTC::SQWAVE_NONE);

  LCD.clear();

  sendReadySemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(sendReadySemaphore);
  // WiFi need to run on core that arduino runs
  xTaskCreatePinnedToCore(
      alarm_clock_task,   /* Function to implement the task */
      "alarm_clock_task", /* Name of the task */
      2048,               /* Stack size in words */
      NULL,               /* Task input parameter */
      1,                  /* Priority of the task */
      &Task0,             /* Task handle. */
      0);                 /* Core where the task should run */
  xTaskCreatePinnedToCore(
      send_mqtt_task,   /* Function to implement the task */
      "send_mqtt_task", /* Name of the task */
      2048,             /* Stack size in words */
      NULL,             /* Task input parameter */
      1,                /* Priority of the task */
      &Task1,           /* Task handle. */
      1);               /* Core where the task should run */

  timer = timerBegin(0, 80, true);             // 80 prescaler for 1MHz clock, count up
  timerAttachInterrupt(timer, &onTimer, true); // Attach ISR
  // timerAlarmWrite(timer, 1800000000, true);  // 30 minutes in microseconds
  timerAlarmWrite(timer, 60000000, true);
  timerAlarmEnable(timer);
  mqtt.setServer(server, 1883);
}

void loop() {}

void get_time(time_comp *time_comp)
{
  Wire.beginTransmission(0x68);
  Wire.write(0); // set register to zero
  Wire.endTransmission();
  Wire.requestFrom(0x68, 3); // 3 bytes (sec, min, hour)
  time_comp->second = bcd2dec(Wire.read() & 0x7f);
  time_comp->minute = bcd2dec(Wire.read());
  time_comp->hour = bcd2dec(Wire.read() & 0x3f);
}

void set_time(time_comp *time_comp)
{
  Wire.beginTransmission(0x68);
  Wire.write(0x00);
  time_comp->second = 0;
  Wire.write(dec2bcd(time_comp->second));
  Wire.write(dec2bcd(time_comp->minute));
  Wire.write(dec2bcd(time_comp->hour));
  Wire.write(0x00);
  Wire.endTransmission();
}

void on_time_set()
{
  set_time(&time_value);
  LCD.clear();
  LCD.setCursor(4, 0);
  LCD.print("Time Set!");
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  LCD.clear();
}

void display_time()
{
  get_time(&time_value);
  LCD.setCursor(0, 0);
  display_position(time_value.hour);
  LCD.print(":");
  display_position(time_value.minute);
  LCD.print(":");
  display_position(time_value.second);
}

void get_date(date_comp *date_comp)
{
  Wire.beginTransmission(0x68);
  Wire.write(4); // set register to 3 (day)
  Wire.endTransmission();
  Wire.requestFrom(0x68, 3); // 3 bytes (day, month, year) - DOW get from Time.h library
  date_comp->day = bcd2dec(Wire.read());
  date_comp->month = bcd2dec(Wire.read());
  date_comp->year = bcd2dec(Wire.read());
}

void set_date(date_comp *date_comp)
{
  Wire.beginTransmission(0x68);
  Wire.write(4);
  // Wire.write(dec2bcd(DoW));
  Wire.write(dec2bcd(date_comp->day));
  Wire.write(dec2bcd(date_comp->month));
  Wire.write(dec2bcd(date_comp->year));
  Wire.endTransmission();
}

void on_date_set()
{
  set_date(&date_value);
  LCD.clear();
  LCD.setCursor(4, 0);
  LCD.print("Date Set!");
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  LCD.clear();
}

void display_date()
{
  get_date(&date_value);
  LCD.setCursor(8, 1);
  display_position(date_value.day);
  LCD.print("/");
  display_position(date_value.month);
  LCD.print("/");
  display_position(date_value.year);

  dow = weekday();
  switch (dow)
  {
  case 1:
    day_of_week = "Sun";
    break;
  case 2:
    day_of_week = "Mon";
    break;
  case 3:
    day_of_week = "Tue";
    break;
  case 4:
    day_of_week = "Wed";
    break;
  case 5:
    day_of_week = "Thu";
    break;
  case 6:
    day_of_week = "Fri";
    break;
  case 7:
    day_of_week = "Sat";
    break;
  }
  LCD.setCursor(0, 1);
  LCD.print(day_of_week);
}

void set_alarm(alarm_comp *alarm_comp)
{
  EEPROM.write(0, alarm_comp->hour);
  EEPROM.write(1, alarm_comp->minute);
  EEPROM.write(2, alarm_comp->active);
  EEPROM.commit();

  RTC.setAlarm(DS3232RTC::ALM1_MATCH_HOURS, 0, alarm_comp->minute, alarm_comp->hour, 0);
  RTC.alarm(DS3232RTC::ALARM_1); // ensure RTC interrupt flag is cleared
  RTC.alarmInterrupt(DS3232RTC::ALARM_1, alarm_comp->active);
}

void get_alarm(alarm_comp *alarm_comp)
{
  alarm_comp->hour = EEPROM.read(0);
  if (alarm_comp->hour > 23)
    alarm_comp->hour = 0;

  alarm_comp->minute = EEPROM.read(1);
  if (alarm_comp->minute > 59)
    alarm_comp->minute = 0;

  alarm_comp->active = EEPROM.read(2);
  if (alarm_comp->active)
  {
    LCD.setCursor(4, 1);
    LCD.print(" ");
    LCD.write(1);
    LCD.print("  ");
  }
  else
  {
    LCD.setCursor(4, 1);
    LCD.print("    ");
  }
}

void on_alarm_set()
{
  set_alarm(&alarm_value);
  LCD.clear();
  LCD.setCursor(4, 0);
  LCD.print("Alarm Set!");
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  LCD.clear();
}

void display_temperature()
{
  int temp_C = dht.readTemperature();

  Datas.temperature = temp_C;

  LCD.setCursor(11, 0);
  LCD.write(4);
  LCD.setCursor(12, 0);
  if (temp_C < 10)
  {
    LCD.print("0");
  }
  LCD.print(temp_C);
  LCD.print((char)223);
  LCD.print("C");
}

void display_humidity()
{
  int hum = dht.readHumidity();

  Datas.humidity = hum;
}

void display_pm()
{
  int index = 0;
  char value;
  char previousValue;

  while (softwareSerial.available())
  {
    value = softwareSerial.read();
    if ((index == 0 && value != 0x42) || (index == 1 && value != 0x4d))
    {
      Serial.println("Cannot find the data header.");
      break;
    }

    if (index > 3 && index % 2 == 0)
    {
      previousValue = value;
    }
    else if (index == 5)
    {
      pm1 = 256 * previousValue + value;
      Datas.pm1_0 = pm1;
    }
    else if (index == 7)
    {
      pm2_5 = 256 * previousValue + value;
      Datas.pm2_5 = pm2_5;
    }
    else if (index == 9)
    {
      pm10 = 256 * previousValue + value;
      Datas.pm10_0 = pm10;
    }
    else if (index > 15)
    {
      break;
    }
    index++;
  }
  while (softwareSerial.available())
    softwareSerial.read();
}

void display_menu(String menu)
{
  LCD.setCursor(6, 0);
  LCD.print("MENU");
  LCD.setCursor(4, 1);
  LCD.print(menu);

  if (!menu.equalsIgnoreCase("SET TIME"))
  {
    LCD.setCursor(0, 1);
    LCD.write(2);
  }
  if (!menu.equalsIgnoreCase("SET ALARM"))
  {
    LCD.setCursor(15, 1);
    LCD.write(3);
  }
}

void on_cancel()
{
  set_time(&time_value);
  LCD.clear();
  LCD.setCursor(4, 0);
  LCD.print("Canceled!");
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  LCD.clear();
}

void on_exit()
{
  blink_state = false;
  button = IDLE;

  LCD.clear();
}

/*
   MAIN
*/
void on_main_enter()
{
  LCD.setCursor(6, 0);
  LCD.write(5);
  LCD.setCursor(7, 0);
  LCD.write(6);
  LCD.setCursor(8, 0);
  LCD.write(7);
  LCD.setCursor(9, 0);
  LCD.write(8);
  LCD.setCursor(1, 1);
  LCD.print("ESP32 & DS3231");
  vTaskDelay(1800 / portTICK_PERIOD_MS);
  LCD.clear();
}

void main_on_state()
{
  get_alarm(&alarm_value);
  display_time();
  display_date();
  display_temperature();
  display_humidity();
  display_pm();

  if (alarm_isr_was_called)
  {
    if (RTC.alarm(DS3232RTC::ALARM_1))
    {
      LCD.clear();

      for (int i = 0; i < 25; i++)
      {
        buttonOK.read();
        if (buttonOK.wasPressed())
        {
          alarm_isr_was_called = false;
          break;
        }
        beep();
      }
    }
    LCD.clear();
  }

  check_buttons();

  switch (button)
  {
  case BUTTON_OK:
    fsm.trigger(BUTTON_OK);
    break;
  }
}

/*
   MENU_SET_TIME
*/
void on_menu_set_time_enter()
{
  display_menu("Set Time");
}

void menu_set_time_on_state()
{
  check_buttons();
  transition(button);
}

/*
   SET_HOUR
*/
void on_set_hour_enter()
{
  LCD.setCursor(4, 0);
  LCD.print("Set Time:");
  LCD.setCursor(10, 1);
  LCD.print("H");
  LCD.setCursor(6, 1);
  LCD.print(":");
  display_position(time_value.minute);
}

void set_hour_on_state()
{
  check_buttons();
  transition(button);

  if (button == BUTTON_UP)
  {
    reset_blink();
    increase(time_value.hour, 23, 0);
  }
  if (button == BUTTON_DOWN)
  {
    reset_blink();
    decrease(time_value.hour, 23, 0);
  }

  if (!long_press_button)
  {
    if (!blink_state)
    {
      LCD.setCursor(4, 1);
      display_position(time_value.hour);
    }
    else
    {
      LCD.setCursor(4, 1);
      LCD.print("  ");
    }
  }
  else
  {
    LCD.setCursor(4, 1);
    display_position(time_value.hour);
  }

  set_blink_state();
}

/*
   SET_MINUTE
*/
void on_set_minute_enter()
{
  LCD.setCursor(4, 0);
  LCD.print("Set Time:");
  LCD.setCursor(10, 1);
  LCD.print("M");
  LCD.setCursor(4, 1);
  display_position(time_value.hour);
  LCD.print(":");
}

void set_minute_on_state()
{
  check_buttons();
  transition(button);

  if (button == BUTTON_UP)
  {
    reset_blink();
    increase(time_value.minute, 59, 0);
  }
  if (button == BUTTON_DOWN)
  {
    reset_blink();
    decrease(time_value.minute, 59, 0);
  }

  if (!long_press_button)
  {
    if (!blink_state)
    {
      LCD.setCursor(7, 1);
      display_position(time_value.minute);
    }
    else
    {
      LCD.setCursor(7, 1);
      LCD.print("  ");
    }
  }
  else
  {
    LCD.setCursor(7, 1);
    display_position(time_value.minute);
  }

  set_blink_state();
}

/*
   MENU_SET_DATE
*/
void on_menu_set_date_enter()
{
  display_menu("Set Date");
}

void menu_set_date_on_state()
{
  check_buttons();
  transition(button);
}

/*
   SET_DAY
*/
void on_set_day_enter()
{
  LCD.setCursor(4, 0);
  LCD.print("Set Date:");
  LCD.setCursor(6, 0);
  LCD.print("/");
  display_position(date_value.month);
  LCD.print("/");
  display_position(date_value.year);
}

void set_day_on_state()
{
  check_buttons();
  transition(button);

  if (button == BUTTON_UP)
  {
    reset_blink();
    increase(date_value.day, 31, 1);
  }
  if (button == BUTTON_DOWN)
  {
    reset_blink();
    decrease(date_value.day, 31, 1);
  }

  if (!long_press_button)
  {
    if (!blink_state)
    {
      LCD.setCursor(4, 1);
      display_position(date_value.day);
    }
    else
    {
      LCD.setCursor(4, 1);
      LCD.print("  ");
    }
  }
  else
  {
    LCD.setCursor(4, 1);
    display_position(date_value.day);
  }

  set_blink_state();
}

/*
   SET_MONTH
*/
void on_set_month_enter()
{
  LCD.setCursor(4, 0);
  LCD.print("Set Date:");
  LCD.setCursor(4, 1);
  display_position(date_value.day);
  LCD.print("/");
  LCD.setCursor(9, 1);
  LCD.print("/");
  display_position(date_value.year);
}

void set_month_on_state()
{
  check_buttons();
  transition(button);

  if (button == BUTTON_UP)
  {
    reset_blink();
    increase(date_value.month, 12, 1);
  }
  if (button == BUTTON_DOWN)
  {
    reset_blink();
    decrease(date_value.month, 12, 1);
  }

  if (!long_press_button)
  {
    if (!blink_state)
    {
      LCD.setCursor(7, 1);
      display_position(date_value.month);
    }
    else
    {
      LCD.setCursor(7, 1);
      LCD.print("  ");
    }
  }
  else
  {
    LCD.setCursor(7, 1);
    display_position(date_value.day);
  }

  set_blink_state();
}

/*
   SET_YEAR
*/
void on_set_year_enter()
{
  LCD.setCursor(4, 0);
  LCD.print("Set Date:");
  LCD.setCursor(4, 1);
  display_position(date_value.day);
  LCD.print("/");
  display_position(date_value.month);
  LCD.print("/");
}

void set_year_on_state()
{
  check_buttons();
  transition(button);

  if (button == BUTTON_UP)
  {
    reset_blink();
    increase(date_value.year, 99, 0);
  }
  if (button == BUTTON_DOWN)
  {
    reset_blink();
    decrease(date_value.year, 99, 0);
  }

  if (!long_press_button)
  {
    if (!blink_state)
    {
      LCD.setCursor(10, 1);
      display_position(date_value.year);
    }
    else
    {
      LCD.setCursor(10, 1);
      LCD.print("  ");
    }
  }
  else
  {
    LCD.setCursor(10, 1);
    display_position(date_value.year);
  }

  set_blink_state();
}

/*
   MENU_SET_ALARM
*/
void on_menu_set_alarm_enter()
{
  display_menu("Set Alarm");
}

void menu_set_alarm_on_state()
{
  check_buttons();
  transition(button);
}

/*
   SET_ALARM_HOUR
*/
void on_set_alarm_hour_enter()
{
  LCD.setCursor(4, 0);
  LCD.print("Set Alarm:");
  LCD.setCursor(12, 1);
  if (alarm_value.active)
  {
    LCD.print("ON");
  }
  else
  {
    LCD.print("OFF");
  }
  LCD.setCursor(6, 1);
  LCD.print(":");
  display_position(alarm_value.minute);
}

void set_alarm_hour_on_state()
{
  check_buttons();
  transition(button);

  if (button == BUTTON_UP)
  {
    reset_blink();
    increase(alarm_value.hour, 23, 0);
  }
  if (button == BUTTON_DOWN)
  {
    reset_blink();
    decrease(alarm_value.hour, 23, 0);
  }

  if (!long_press_button)
  {
    if (!blink_state)
    {
      LCD.setCursor(4, 1);
      display_position(alarm_value.hour);
    }
    else
    {
      LCD.setCursor(4, 1);
      LCD.print("  ");
    }
  }
  else
  {
    LCD.setCursor(4, 1);
    display_position(alarm_value.hour);
  }

  set_blink_state();
}

/*
   SET_ALARM_MINUTE
*/
void on_set_alarm_minute_enter()
{
  LCD.setCursor(4, 0);
  LCD.print("Set Alarm:");
  LCD.setCursor(12, 1);
  if (alarm_value.active)
  {
    LCD.print("ON");
  }
  else
  {
    LCD.print("OFF");
  }
  LCD.setCursor(4, 1);
  display_position(alarm_value.hour);
  LCD.print(":");
}

void set_alarm_minute_on_state()
{
  check_buttons();
  transition(button);

  if (button == BUTTON_UP)
  {
    reset_blink();
    increase(alarm_value.minute, 59, 0);
  }
  if (button == BUTTON_DOWN)
  {
    reset_blink();
    decrease(alarm_value.minute, 59, 0);
  }

  if (!long_press_button)
  {
    if (!blink_state)
    {
      LCD.setCursor(7, 1);
      display_position(alarm_value.minute);
    }
    else
    {
      LCD.setCursor(7, 1);
      LCD.print("  ");
    }
  }
  else
  {
    LCD.setCursor(7, 1);
    display_position(alarm_value.minute);
  }

  set_blink_state();
}

/*
   SET_ALARM_ON_OFF
*/
void on_set_alarm_on_off_enter()
{
  LCD.setCursor(4, 0);
  LCD.print("Set Alarm:");
  LCD.setCursor(7, 1);
  if (alarm_value.active)
  {
    LCD.print("ON");
  }
  else
  {
    LCD.print("OFF");
  }
  LCD.setCursor(4, 1);
  display_position(alarm_value.hour);
  LCD.print(":");
  display_position(alarm_value.minute);
  LCD.print("  ");
}

void set_alarm_on_off_on_state()
{
  check_buttons();
  transition(button);

  if (button == BUTTON_UP || button == BUTTON_DOWN)
  {
    reset_blink();
    alarm_value.active = !alarm_value.active;
  }

  if (alarm_value.active)
  {
    if (!blink_state)
    {
      LCD.setCursor(14, 1);
      LCD.print(" ");
      LCD.setCursor(12, 1);
      LCD.print("ON");
    }
    else
    {
      LCD.setCursor(12, 1);
      LCD.print("  ");
    }
  }
  else
  {
    if (!blink_state)
    {
      LCD.setCursor(12, 1);
      LCD.print("OFF");
    }
    else
    {
      LCD.setCursor(12, 1);
      LCD.print("   ");
    }
  }

  set_blink_state();
}

void set_blink_state()
{
  unsigned long blink_currentMillis = millis();
  if (blink_currentMillis - blink_previousMillis > blink_interval)
  {
    blink_previousMillis = blink_currentMillis;
    if (!blink_state)
    {
      blink_state = true;
    }
    else
    {
      blink_state = false;
    }
  }
}

void check_buttons()
{
  buttonLeft.read();
  buttonRight.read();
  buttonUp.read();
  buttonDown.read();
  buttonOK.read();
  buttonBack.read();

  if (buttonLeft.wasPressed())
  {
    button = BUTTON_LEFT;
  }
  if (buttonRight.wasPressed())
  {
    button = BUTTON_RIGHT;
  }
  if (buttonUp.wasPressed())
  {
    button = BUTTON_UP;
  }
  if (buttonDown.wasPressed())
  {
    button = BUTTON_DOWN;
  }
  if (buttonOK.wasPressed())
  {
    button = BUTTON_OK;
  }
  if (buttonBack.wasPressed())
  {
    button = BUTTON_BACK;
  }

  if (buttonUp.wasReleased())
  {
    long_press_button = false;
    rpt = REPEAT_FIRST;
  }
  if (buttonUp.pressedFor(rpt))
  {
    rpt += REPEAT_INCR;
    long_press_button = true;
    button = BUTTON_UP;
  }

  if (buttonDown.wasReleased())
  {
    long_press_button = false;
    rpt = REPEAT_FIRST;
  }
  if (buttonDown.pressedFor(rpt))
  {
    rpt += REPEAT_INCR;
    long_press_button = true;
    button = BUTTON_DOWN;
  }
}

void transition(BUTTONS trigger)
{
  if (trigger != BUTTON_UP || trigger != BUTTON_DOWN)
  {
    fsm.trigger(trigger);
  }
}

void reset_blink()
{
  button = IDLE;
  blink_state = false;
  blink_previousMillis = millis();
}

void increase(int &number, int max, int min)
{
  number++;
  if (number > max)
    number = min;
}

void decrease(int &number, int max, int min)
{
  number--;
  if (number < min)
    number = max;
}

void beep()
{
  digitalWrite(ALARM_OUT, HIGH);
  LCD.noBacklight();
  vTaskDelay(500 / portTICK_PERIOD_MS);
  digitalWrite(ALARM_OUT, LOW);
  LCD.backlight();
  vTaskDelay(500 / portTICK_PERIOD_MS);
}

byte dec2bcd(byte val)
{
  return ((val / 10 * 16) + (val % 10));
}

byte bcd2dec(byte val)
{
  return ((val / 16 * 10) + (val % 16));
}

void display_position(int digits)
{
  if (digits < 10)
    LCD.print("0");
  LCD.print(digits);
}

void alarm_isr()
{
  alarm_isr_was_called = true;
}
