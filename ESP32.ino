#define ERA_DEBUG
#define ERA_LOCATION_VN
#define ERA_AUTH_TOKEN "376a5f04-6b2c-42e0-8d71-9ff738586861"

#include <Arduino.h>
#include <ERa.hpp>
#include <PZEM004Tv30.h>       
#include <Wire.h>              
#include <LiquidCrystal_I2C.h> 
#include <RTClib.h>            
#include <Keypad.h>            
#include <EEPROM.h>            

#define PZEM1_RX    32   
#define PZEM1_TX    33  
#define PZEM2_RX    26 
#define PZEM2_TX    27   
#define MQ2_PIN     34   
#define BUZZER_PIN  13 
#define LED_PIN     12    
#define RELAY1_PIN  25 
#define RELAY2_PIN  14 

#define EEPROM_SIZE 64
#define EEPROM_LASTDAY_ADDR              0
#define EEPROM_LASTMONTH_ADDR           20
#define EEPROM_ENERGY_START_DAY1_ADDR   40
#define EEPROM_ENERGY_START_DAY2_ADDR   44
#define EEPROM_ENERGY_START_MONTH1_ADDR 48
#define EEPROM_ENERGY_START_MONTH2_ADDR 52

const char ssid[] = "Do An";
const char pass[] = "12345678";

HardwareSerial mySerial1(1); 
HardwareSerial mySerial2(2); 
PZEM004Tv30 pzem1(&mySerial1, PZEM1_RX, PZEM1_TX);
PZEM004Tv30 pzem2(&mySerial2, PZEM2_RX, PZEM2_TX);

LiquidCrystal_I2C lcd1(0x27, 20, 4); 
LiquidCrystal_I2C lcd2(0x26, 20, 4);

RTC_DS1307 rtc;

int voltageLimitDevice1 = 230;
int powerLimitDevice1 = 100;  
int voltageLimitDevice2 = 230; 
int powerLimitDevice2 = 100;  

int currentDevice = 1;    
int setupMode = 0;        
String input = "";        
bool isSettingMode = false; 
unsigned long lastUpdate = 0; 

float electricityCost = 0.0; 
float dailyElectricityCost = 0.0; 
float monthlyElectricityCost = 0.0; 
float energyAtStartOfDay1 = 0.0;  
float energyAtStartOfDay2 = 0.0; 
float energyAtStartOfMonth1 = 0.0; 
float energyAtStartOfMonth2 = 0.0;
DateTime lastDay; 
DateTime lastMonth;

const byte ROWS = 4; 
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};
byte rowPins[ROWS] = {15, 2, 0, 4}; 
byte colPins[COLS] = {16, 17, 5};   
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

bool relay1State = 1; 
bool relay2State = 1; 

void showLimitScreen();      
void handleKeyInput(char key);  
void readAndDisplayPzemData(); 
void loadFromEEPROM();
void saveToEEPROM();

ERA_WRITE(V16) {
  relay1State = param.getInt();
}
ERA_WRITE(V17) {
  relay2State = param.getInt();
}

void setup() {
  #if defined(ERA_DEBUG)
    Serial.begin(115200);  
  #endif
  ERa.setScanWiFi(true);  
  ERa.begin(ssid, pass);   

  mySerial1.begin(9600, SERIAL_8N1, PZEM1_RX, PZEM1_TX); 
  mySerial2.begin(9600, SERIAL_8N1, PZEM2_RX, PZEM2_TX); 

  lcd1.begin();
  lcd1.backlight();
  lcd2.begin();
  lcd2.backlight();

  Wire.begin(21, 22); 
  if (!rtc.begin()) {
    Serial.println("Không tìm thấy DS1307!");
    while (1);
  }
  if (!rtc.isrunning()) {
    Serial.println("DS1307 không chạy, thiết lập lại thời gian.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); 
  }
  
  pinMode(MQ2_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);

  digitalWrite(RELAY1_PIN, relay1State);
  digitalWrite(RELAY2_PIN, relay2State);
  
  EEPROM.begin(EEPROM_SIZE);
  loadFromEEPROM();
  showLimitScreen(); 
}

ERA_CONNECTED() {
  Serial.println("Đã kết nối với ERa thành công!");
}
ERA_DISCONNECTED() {
  Serial.println("Đã mất kết nối với ERa!");
}

void loop() {
  ERa.run();  
  readAndDisplayPzemData();  
  char key = keypad.getKey();
  if (key) {
    handleKeyInput(key);
  }

  if (!isSettingMode && millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    readAndDisplayPzemData();
    showLimitScreen(); 
  }
}

void readAndDisplayPzemData() {
  float voltage1 = isnan(pzem1.voltage()) ? 0.0 : pzem1.voltage();
  float current1 = pzem1.current();
  float power1 = pzem1.power();
  float energy1 = pzem1.energy();
  float frequency1 = pzem1.frequency();
  float pf1 = pzem1.pf();

  float voltage2 = isnan(pzem2.voltage()) ? 0.0 : pzem2.voltage();
  float current2 = pzem2.current();
  float power2 = pzem2.power();
  float energy2 = pzem2.energy();
  float frequency2 = pzem2.frequency();
  float pf2 = pzem2.pf();

  electricityCost = (energy1 + energy2) * 3000;
  float device1energyday = energy1 - energyAtStartOfDay1;
  float device2energyday = energy2 - energyAtStartOfDay2;
  if (device1energyday < 0) {device1energyday = 0;}
  if (device2energyday < 0) {device2energyday = 0;}
  dailyElectricityCost = (device1energyday + device2energyday) * 3000;

  float device1energymon = energy1 - energyAtStartOfMonth1;
  float device2energymon = energy2 - energyAtStartOfMonth2;
  if (device1energymon < 0) {device1energymon = 0;}
  if (device2energymon < 0) {device2energymon = 0;}
  monthlyElectricityCost = (device1energymon + device2energymon) * 3000;

  DateTime now = rtc.now(); 
  if (now.day() != lastDay.day()) { 
    energyAtStartOfDay1 = energy1; 
    energyAtStartOfDay2 = energy2; 
    lastDay = now; 
    saveToEEPROM();
  } 
  if (now.month() != lastMonth.month()) { 
    energyAtStartOfMonth1 = energy1; 
    energyAtStartOfMonth2 = energy2; 
    lastMonth = now;
    saveToEEPROM();
  }
  
  int mq2Value = analogRead(MQ2_PIN);
  float gasConcentration = mq2Value / 40.95; 

  sendDataToEra(voltage1, current1, power1, device1energyday, voltage2, current2, power2, device2energyday, electricityCost, gasConcentration, device1energymon, pf1, device2energymon, pf2, dailyElectricityCost, monthlyElectricityCost);
  controlRelay(voltage1, power1, voltage2, power2, gasConcentration);
  displayPzemDataOnLCD(voltage1, current1, power1, device1energymon, voltage2, current2, power2, device2energymon);
}

void controlRelay(float voltage1, float power1, float voltage2, float power2, float gasConcentration) {
    bool newRelay1State = (voltage1 > voltageLimitDevice1 || power1 > powerLimitDevice1 || gasConcentration > 30.0) ? LOW : HIGH;
    bool newRelay2State = (voltage2 > voltageLimitDevice2 || power2 > powerLimitDevice2 || gasConcentration > 30.0) ? LOW : HIGH;
    if (newRelay1State == 0) {
        relay1State = newRelay1State;
    }
    digitalWrite(RELAY1_PIN, relay1State);
    ERa.virtualWrite(V16, digitalRead(RELAY1_PIN));
    if (newRelay2State == 0) {
        relay2State = newRelay2State;
    }
    digitalWrite(RELAY2_PIN, relay2State);
    ERa.virtualWrite(V17, digitalRead(RELAY2_PIN));
    bool alert = (relay1State == LOW || relay2State == LOW);
    digitalWrite(BUZZER_PIN, alert ? HIGH : LOW);
    digitalWrite(LED_PIN, alert ? HIGH : LOW);
}

void displayPzemDataOnLCD(float voltage1, float current1, float power1, float device1energymon, float voltage2, float current2, float power2, float device2energymon) {
    lcd2.setCursor(0, 0);
    lcd2.printf("C1:%.2fA  ", current1);
    lcd2.setCursor(10, 0);             
    lcd2.printf("V1:%.1fV ", voltage1); 
    lcd2.setCursor(0, 1);
    lcd2.printf("P1:%.1fW   ", power1);
    lcd2.setCursor(10, 1);
    lcd2.printf("E1:%.2fkWh", device1energymon);

    lcd2.setCursor(0, 2);
    lcd2.printf("C2:%.2fA  ", current2);
    lcd2.setCursor(10, 2);             
    lcd2.printf("V2:%.1fV ", voltage2); 
    lcd2.setCursor(0, 3);
    lcd2.printf("P2:%.1fW   ", power2);
    lcd2.setCursor(10, 3);
    lcd2.printf("E2:%.2fkWh", device2energymon);
}

void showLimitScreen() {
    DateTime now = rtc.now();
    int mq2Value = analogRead(MQ2_PIN);
    float gasConcentration = mq2Value / 40.95;
  
    lcd1.setCursor(0, 0);
    lcd1.printf("V1Li:%dV P1Li:%dW", voltageLimitDevice1, powerLimitDevice1);
    lcd1.setCursor(0, 1);
    lcd1.printf("V2Li:%dV P2Li:%dW", voltageLimitDevice2, powerLimitDevice2);
    lcd1.setCursor(0, 2);
    lcd1.printf("G:%.1f%%  ", gasConcentration);
    lcd1.setCursor(9, 2);
    lcd1.printf("T:%.0fVND  ", monthlyElectricityCost);
    lcd1.setCursor(0, 3);
    lcd1.printf("%02d:%02d:%02d %02d/%02d/%04d ", now.hour(), now.minute(), now.second(), now.day(), now.month(), now.year());
}

void handleKeyInput(char key) {
  if (key == '*') { 
    isSettingMode = true; 
    if (setupMode == 1 && input.length() > 0) { 
      if (currentDevice == 1) voltageLimitDevice1 = input.toInt();
      else voltageLimitDevice2 = input.toInt();
      showLimitScreen();
    }
    else if (setupMode == 2 && input.length() > 0) { 
      if (currentDevice == 1) powerLimitDevice1 = input.toInt();
      else powerLimitDevice2 = input.toInt();
      showLimitScreen();
    }
    setupMode++;
    input = "";  
    if (setupMode > 2) {
      setupMode = 0; 
      isSettingMode = false; 
      showLimitScreen();
    } 
    else {
      lcd1.clear();
      lcd1.setCursor(0, 0);
      lcd1.print("Thiet bi ");
      lcd1.print(currentDevice);
      lcd1.setCursor(0, 1);
      if (setupMode == 1) {
        lcd1.print("Cai dat Dien ap:");
      } 
      else if (setupMode == 2) {
        lcd1.print("Cai dat Cong suat:");
      }
    }
  } 
  else if (key == '#') { 
    currentDevice = (currentDevice == 1) ? 2 : 1;
    setupMode = 0;
    isSettingMode = false; 
    showLimitScreen();
  } 
  else if (setupMode == 1 || setupMode == 2) { 
    input += key;
    if (input.length() > 3) {
      input = input.substring(input.length() - 3); 
    }
    lcd1.setCursor(0, 2);
    lcd1.print("Nhap: ");
    lcd1.setCursor(6, 2);
    lcd1.print(input);
  }
}

void sendDataToEra(float voltage1, float current1, float power1, float device1energyday, float voltage2, float current2, float power2, float device2energyday, float electricityCost, float gasConcentration, float device1energymon, float pf1, float device2energymon, float pf2, float dailyElectricityCost, float monthlyElectricityCost) {
    ERa.virtualWrite(V0, voltage1);         
    ERa.virtualWrite(V1, current1);         
    ERa.virtualWrite(V2, power1);           
    ERa.virtualWrite(V3, device1energyday);          
    ERa.virtualWrite(V4, voltage2);         
    ERa.virtualWrite(V5, current2);         
    ERa.virtualWrite(V6, power2);           
    ERa.virtualWrite(V7, device2energyday);        
    ERa.virtualWrite(V8, electricityCost);  
    ERa.virtualWrite(V9, gasConcentration); 
    ERa.virtualWrite(V10, device1energymon);      
    ERa.virtualWrite(V11, pf1);             
    ERa.virtualWrite(V12, device2energymon);      
    ERa.virtualWrite(V13, pf2);             
    ERa.virtualWrite(V14, dailyElectricityCost);
    ERa.virtualWrite(V15, monthlyElectricityCost);
}

void saveToEEPROM() {
  EEPROM.put(EEPROM_LASTDAY_ADDR, lastDay.unixtime());
  EEPROM.put(EEPROM_LASTMONTH_ADDR, lastMonth.unixtime());
  EEPROM.put(EEPROM_ENERGY_START_DAY1_ADDR, energyAtStartOfDay1);
  EEPROM.put(EEPROM_ENERGY_START_DAY2_ADDR, energyAtStartOfDay2);
  EEPROM.put(EEPROM_ENERGY_START_MONTH1_ADDR, energyAtStartOfMonth1);
  EEPROM.put(EEPROM_ENERGY_START_MONTH2_ADDR, energyAtStartOfMonth2);
  EEPROM.commit();
}

void loadFromEEPROM() {
  unsigned long lastDayTime, lastMonthTime;
  EEPROM.get(EEPROM_LASTDAY_ADDR, lastDayTime);
  EEPROM.get(EEPROM_LASTMONTH_ADDR, lastMonthTime);
  lastDay = DateTime(lastDayTime);
  lastMonth = DateTime(lastMonthTime);
  EEPROM.get(EEPROM_ENERGY_START_DAY1_ADDR, energyAtStartOfDay1);
  EEPROM.get(EEPROM_ENERGY_START_DAY2_ADDR, energyAtStartOfDay2);
  EEPROM.get(EEPROM_ENERGY_START_MONTH1_ADDR, energyAtStartOfMonth1);
  EEPROM.get(EEPROM_ENERGY_START_MONTH2_ADDR, energyAtStartOfMonth2);
}
