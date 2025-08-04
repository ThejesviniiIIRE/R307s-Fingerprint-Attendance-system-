#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Adafruit_Fingerprint.h>
#include <EEPROM.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const char* ssid = "Redmi Note 10S";
const char* password = "9994646359";
const char* serverUrl = "https://testing.indrainsignia.co.in/fingerprint/data.php";
#define ID_SIZE 6
#define ID_START (STATUS_START + MAX_USERS * STATUS_SIZE)
//bool simulateServerDown = false;
#include <SoftwareSerial.h>

// Define RX, TX pins 
#define RX_PIN  12 
#define TX_PIN  14  

SoftwareSerial mySerial(RX_PIN, TX_PIN);
//---for enrollment-
//unsigned long lastCheck = 0;
//const unsigned long checkInterval = 3000; // every 5 seconds
//bool enroll = false;
//-----for servercheck----
//bool serverIsOnline = false;
//unsigned long lastCheckTime = 0;
//const unsigned long checkInterval = 10000;  // 10 seconds
//bool store = false;
//bool push = true;
const char* serverURL = "https://testing.indrainsignia.co.in/fingerprint/data.php";//server error url
//unsigned long lastServerCheck = 0;
//const unsigned long serverCheckInterval = 30000; // Check every 30 seconds
Adafruit_Fingerprint finger(&mySerial);

#define NAME_SIZE 20
#define TIME_SIZE 8
#define STATUS_SIZE 2
#define MAX_USERS 100
#define NAME_START 0
#define TIME_START (NAME_START + MAX_USERS * NAME_SIZE)
#define STATUS_START (TIME_START + MAX_USERS * TIME_SIZE)

#define RECORD_SIZE 33  // ID(6) + Name(8) + Date(10) + Time(8) + Type(1)
uint32_t currentEEPROMAddress = 0;
#define EEPROM_SIZE 262144  // for 2Mbit M24M02 EEPROM
#define MAX_RECORDS (EEPROM_SIZE / RECORD_SIZE)
#define EEPROM_I2C_BASE_ADDR 0x50  // M24M02 base I2C address for clear
#define EEPROM_I2C_ADDRESS 0x50
#define PAGE_SIZE 256              // Page size in bytes
//bool serverBackOnline = false;
bool inFlags[MAX_USERS];
bool outFlags[MAX_USERS];
int lastDayOfYear[MAX_USERS];  // To reset flags daily

LiquidCrystal_I2C lcd(0x27, 16, 2);  // Change 0x27 to 0x3F if your LCD uses a different I2C address

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);  // IST offset
//----Time sync------
void waitForTimeSync() {
  Serial.print("Syncing time");
  lcd.setCursor(0, 0);
  lcd.print("Syncing time    ");
  delay(1000);
  while (!timeClient.update()) {
    timeClient.forceUpdate();
    delay(500);
    Serial.print(".");
    //lcd.print(".");
  }
  Serial.println("\nTime synced");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Time synced     ");
  delay(1000);
}

void setup() {
  Serial.begin(9600);
  delay(1000);
//--------WiFi setup-----------
  WiFi.begin(ssid, password);
   Wire.begin(D2, D1);
lcd.begin(16, 2); 
   delay(1000);
lcd.backlight();
lcd.clear();
lcd.setCursor(0, 0);
lcd.print("System Ready");
delay(1000);
 
  Serial.print("Connecting WiFi ");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  delay(1000);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    //lcd.print(".");
  }
  Serial.println("\nWiFi connected");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi connected  ");
delay(1000);
  


  timeClient.begin();
 
  waitForTimeSync();

  EEPROM.begin(4096);
//--------------Fingerprint sensor detection---------
  finger.begin(57600);
  if (!finger.verifyPassword()) {
    Serial.println("Fingerprint sensor not found :");
    lcd.clear();
    lcd.print("sensor not found : ");
    while (1) delay(1);
  }

  Serial.print("Fingerprint sensor ready with ");
  
  finger.getTemplateCount();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("ready with      ");
  lcd.setCursor(0,1);
  lcd.print(finger.templateCount);
  lcd.setCursor(2,1);
  lcd.print("  templates.");
  Serial.print(finger.templateCount);
  
  Serial.println(" templates.");
  delay(1000);
  Serial.println("Give clear command to clear the internal EEprom and r307s memory");
   lcd.setCursor(0,0);
  lcd.print("verification");

   delay(1000);
}


//const unsigned long checkInterval = 10000;  // 10 seconds

unsigned long lastServerCheck = 0;
unsigned long lastEnrollmentCheck = 0;
unsigned long lastVerifyCheck = 0;

const unsigned long serverCheckInterval = 15000;       // 15 sec
const unsigned long enrollmentCheckInterval = 15000;   // 15 sec
const unsigned long verifyInterval = 1000;             // 1 sec

bool push = false;
bool store = false;
bool enroll = false;

void loop() {
  unsigned long currentMillis = millis();

  // ----------- 1. Periodic Server Status Check -----------
  if (currentMillis - lastServerCheck >= serverCheckInterval) {
    lastServerCheck = currentMillis;
    
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    String url = "https://testing.indrainsignia.co.in/fingerprint/data.php";
    String requestBody = "{}";

    if (https.begin(client, url)) {
      https.addHeader("Content-Type", "application/json");
      int httpCode = https.POST(requestBody);
      Serial.println("HTTP code: " + String(httpCode));

      if (httpCode == 200 || httpCode == 400) {
        if (!push) {
          Serial.println("Server just came online.");
          push = true;
        } else {
          Serial.println("Server is still online.");
        }
      } else {
        if (push) Serial.println("Server went offline.");
        push = false;
      }

      https.end();
    } else {
      push = false;
      Serial.println("HTTPS.begin() failed");
    }
  }

  // ----------- 2. Periodic Enrollment Check if Server is Online -----------
  if (push && currentMillis - lastEnrollmentCheck >= enrollmentCheckInterval) {
    lastEnrollmentCheck = currentMillis;
    checkForEnrollment();
  }

  // ----------- 3. EEPROM Sync After Server Recovery -----------
  if (push && store) {
    Serial.println("Server is UP. Syncing offline EEPROM data...");
    readOfflineRecords();
    syncOfflineDataToServer();
    clearExternalEEPROM();
    store = false;
  }

  // ----------- 4. Serial Command Handling -----------
  if (Serial.available() && push) {
    String in = Serial.readStringUntil('\n');
    in.trim();

    if (in.equalsIgnoreCase("clear")) {
      clearAll();
      delay(100);
      clearExternalEEPROM();
    } else if (in.startsWith("delete")) {
      deleteID(in.substring(6).toInt());
    } else {
      Serial.println("Invalid command.");
    }
  }

  // ----------- 5. Continuous Fingerprint Verification -----------
  if (!enroll && currentMillis - lastVerifyCheck >= verifyInterval) {
    lastVerifyCheck = currentMillis;
    verifyAndLog();  // Make sure this function is non-blocking
  }
}


//----------------------Verification function module-------------
void verifyAndLog() {
  Serial.println("Waiting for finger...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan Finger...");
  delay(2000); 

  if (finger.getImage() != FINGERPRINT_OK) return;
  if (finger.image2Tz() != FINGERPRINT_OK) {
    Serial.println("Image conversion failed");
     lcd.setCursor(0, 0);
    lcd.print("Image failed");
    return;
  }

  if (finger.fingerSearch() != FINGERPRINT_OK) {
    Serial.println("Fingerprint not recognized");
    lcd.setCursor(0,0);
    lcd.print("Fingerprint");
    lcd.setCursor(0,1);
    lcd.print("not recognised");
    return;
  }

  int address = finger.fingerID;
  String name = readNameFromEEPROM(address);
  int empID = readIDFromEEPROM(address);
Serial.println("Employee ID: " + String(empID));

  Serial.println("Welcome " + name);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hello");
  delay(1000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(name);
  lcd.setCursor(0, 1);
  lcd.print(empID);
  delay(2000); 

  timeClient.update();
  time_t raw = timeClient.getEpochTime();
  struct tm *ti = localtime(&raw);

  char dateBuf[11], timeBuf[9];
  sprintf(dateBuf, "%04d-%02d-%02d", ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
  sprintf(timeBuf, "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
  String dateStr = String(dateBuf);
  String timeStr = String(timeBuf);

int currentDay = ti->tm_yday;
  bool isClockIn = processPunchFlag(address, currentDay);

  

  // Reset flags if it's a new day
    if (isClockIn) {
    Serial.println("Clock-in detected");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Clocked In");
    lcd.setCursor(0, 1);
    lcd.print(timeStr);
    delay(2000);

    pushToServer(address, name, dateStr, timeStr, "");  // Clock-in
  }
  else {
    Serial.println("Clock-out detected");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Clocked Out");
    lcd.setCursor(0, 1);
    lcd.print(timeStr);
    delay(2000);

    pushToServer(address, name, dateStr, "", timeStr);  // Clock-out
  }

}
bool processPunchFlag(int address, int today) {
  int base = STATUS_START + address * STATUS_SIZE;
  uint8_t lastDay = EEPROM.read(base);
  uint8_t flag = EEPROM.read(base + 1);

  if (lastDay != today) {
    EEPROM.write(base, today);
    EEPROM.write(base + 1, 1); // clock-in
    EEPROM.commit();
    return true;
  } else if (flag == 1) {
    EEPROM.write(base + 1, 2); // clock-out
    EEPROM.commit();
    return false;
  }
  return false;
}

//-------------server push------------
void pushToServer(int address, String name, String dateStr, String timeStr, String outTimeStr) {
  HTTPClient https;
  WiFiClientSecure client;
  client.setInsecure();
  
  String url = "https://testing.indrainsignia.co.in/fingerprint/data.php"; // Update this
  https.begin(client, url);
 
  StaticJsonDocument<200> doc;
  int empID = readIDFromEEPROM(address);
   /*doc["id"] = id;
  doc["name"] = name;
  doc["date"] = dateStr;
  doc["in"] = timeStr;
  doc["out"] = outTimeStr;  // If empty, sends "out": ""*/
  if (outTimeStr == "") {
    // Clock-in
    doc["id"] = empID;
    doc["name"] = name;
    doc["date"] = dateStr;
    doc["in"] = timeStr;
    //doc["out"] = ;//empty
  } else {
    // Clock-out
    doc["id"] = empID;
    doc["name"] = name;
    doc["date"] = dateStr;
   // doc["in"] = timeStr;
    doc["out"] = outTimeStr;
  }

  String requestBody;
  serializeJson(doc, requestBody);

  https.addHeader("Content-Type", "application/json");
  int httpCode = https.POST(requestBody);
Serial.println("HTTP code: " + String(httpCode));
  if ( httpCode == 200 && push) {
    push=true;
    String response = https.getString();
    Serial.println("HTTP code: " + String(httpCode));
    Serial.println("Server ACK: " + response);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Data Sent");
    /*lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Server ACK:");
    lcd.setCursor(0, 1);
    lcd.print(response.substring(0, 16));  // Truncate for LCD
//Exteeprom=false;*/
  } 
  else /*if (!serverIsOnline)*/{
    push=false;
  Serial.println("POST failed or simulated down");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Server Error");

  char type = (outTimeStr == "") ? 'I' : 'O';
  String record = makeFixedLengthRecord(String(empID), name, dateStr, (outTimeStr == "" ? timeStr : outTimeStr), type);
  writeRecord(currentEEPROMAddress, record);
  currentEEPROMAddress += RECORD_SIZE;
  store=true;
  
  Serial.println("Data stored to EEPROM (simulated server fail).");
  lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Data Stored");
}
//Serial.println("push " + push);
//Serial.println("store " + store);
  https.end();
}



void doEnroll() {
  int address = findNextAvailableID();
  if (address < 1) {
    Serial.println("No space left.");
    return;
  }

  /*Serial.print("Enroll at address ");
  Serial.println(address);
  Serial.println("Enter name:");
   lcd.clear();
   lcd.setCursor(0, 0);
  lcd.print("Enroll at address ");
   lcd.setCursor(0, 1);
  lcd.print(address);
  delay(1000);
  lcd.clear();
   lcd.setCursor(0, 0);
  lcd.print("Enter name:");*/

  while (!Serial.available()) delay(1000);
  String name = Serial.readStringUntil('\n');
  name.trim();

  bool success = false;
  while (!success) {
    Serial.println("Place finger to enroll...");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Place finger");
    delay(2000);
    success = enrollFingerprint(address);
    if (!success) Serial.println("Enrollment failed. Trying again...");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Enrollment failed.");
    lcd.setCursor(0,1);
    lcd.print("Trying again...");
  }

  storeUserInEEPROM(address, name, address);  // address is being used as empID here

  Serial.println("Enrolled " + name);
  lcd.clear();
   lcd.setCursor(0,0);
  lcd.print("Enrolled");
   lcd.setCursor(0,1);
    lcd.print(name);
  delay(5000);
}

bool enrollFingerprint(uint8_t address) {
  int p;
  Serial.println("Place finger...");
   lcd.clear();
   lcd.setCursor(0,0);
  lcd.print("Enroll Begin");
  delay(1000);
  lcd.clear();
   lcd.setCursor(0,0);
  lcd.print("Place finger...");
  delay(2000);

  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) Serial.print(".");
    //lcd.print(".");
    delay(1000);
  }

  if (finger.image2Tz(1) != FINGERPRINT_OK) return false;

  Serial.println("\nRemove finger...");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Remove finger");
  delay(1500);
  while (finger.getImage() != FINGERPRINT_NOFINGER) delay(200);

  Serial.println("Keep same finger");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Keep same finger");
  
  while ((p = finger.getImage()) != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) Serial.print(".");
    //lcd.print(".");
    delay(1000);
  }

  if (finger.image2Tz(2) != FINGERPRINT_OK) return false;
  if (finger.createModel() != FINGERPRINT_OK) return false;
  return (finger.storeModel(address) == FINGERPRINT_OK);
}

void clearAll() {
  if (finger.emptyDatabase() == FINGERPRINT_OK) {
    clearEEPROM();
    Serial.println("Database cleared!");
    lcd.clear();
   lcd.setCursor(0,0);
    lcd.print("Database cleared!");
  } else Serial.println("Clear failed!");
}

void deleteID(int address) {
  if (address < 1 || address > 127) {
    Serial.println("address must be 1–127");
    lcd.clear();
   lcd.setCursor(0,0);
    lcd.print("address must be 1–100");
    return;
  }

  if (finger.deleteModel(address) == FINGERPRINT_OK) {
    storeUserInEEPROM(address, "", 0);

    Serial.println("Deleted ID " + String(address));
    lcd.clear();
   lcd.setCursor(0,0);
    lcd.print("Deleted ID " + String(address));
  } else Serial.println("Delete failed.");
  lcd.clear();
   lcd.setCursor(0,0);
  lcd.print("Delete failed.");
}

void storeUserInEEPROM(uint8_t address, const String& name, int empID) {
  int nameBase = NAME_START + address * NAME_SIZE;
  for (int i = 0; i < NAME_SIZE; i++)
    EEPROM.write(nameBase + i, i < name.length() ? name[i] : 0);

  int idBase = ID_START + address * ID_SIZE;
  EEPROM.write(idBase, (empID >> 24) & 0xFF);
  EEPROM.write(idBase + 1, (empID >> 16) & 0xFF);
  EEPROM.write(idBase + 2, (empID >> 8) & 0xFF);
  EEPROM.write(idBase + 3, empID & 0xFF);

  EEPROM.commit();
}


int readIDFromEEPROM(uint8_t address) {
  int idBase = ID_START + address * ID_SIZE;
  int id = (EEPROM.read(idBase) << 24) |
           (EEPROM.read(idBase + 1) << 16) |
           (EEPROM.read(idBase + 2) << 8) |
           EEPROM.read(idBase + 3);
  return id;
}
String readNameFromEEPROM(uint8_t id) {
  char buf[NAME_SIZE + 1];
  int base = NAME_START + id * NAME_SIZE;
  for (int i = 0; i < NAME_SIZE; i++) buf[i] = EEPROM.read(base + i);
  buf[NAME_SIZE] = 0;
  return String(buf);
}

void clearEEPROM() {
  for (int i = 0; i < 4096; i++) EEPROM.write(i, 0);
  EEPROM.commit();
}

int findNextAvailableID() {
  for (int address = 1; address <= 127; address++)
    if (finger.loadModel(address) != FINGERPRINT_OK) return address;
  return -1;
}
//-----------Enrollment----------------
void checkForEnrollment() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = String("https://testing.indrainsignia.co.in/fingerprint/data.php?get") ;//+ "?action=pending"
  if (!https.begin(client, url)) {
    Serial.println("Failed to connect to server");
    return;
  }

  int httpCode = https.GET();
  
  if (httpCode > 0) {
    enroll=true;
    String response = https.getString();
    Serial.println("Enrollment JSON: " + response);

StaticJsonDocument<512> doc;
DeserializationError error = deserializeJson(doc, response);

if (!error && doc.is<JsonArray>()) {
  JsonArray arr = doc.as<JsonArray>();

  for (JsonObject obj : arr) {
    if (obj.containsKey("EmployeeID") && obj.containsKey("EmployeeName")) {
      int empID = obj["EmployeeID"].as<int>();  // or .toInt() if it's a String
      String empName = obj["EmployeeName"].as<String>();

      Serial.println("Received enrollment request from server:");
      Serial.println("   -> ID   : " + String(empID));
      Serial.println("   -> Name : " + empName);

      doEnrollFromServer(empID, empName);
    }
  }
} else {
  Serial.println("Invalid or empty JSON received.");
}


    if (!error && doc.containsKey("EmployeeID") && doc.containsKey("EmployeeName")) {
      int empID = doc["EmployeeID"];
      String empName = doc["EmployeeName"].as<String>();

      doEnrollFromServer(empID, empName);
    }
  }
   
  https.end();
  enroll=false;
}

void doEnrollFromServer(int empID, const String& empName) {
  /*if (finger.loadModel(empID) == FINGERPRINT_OK) {
    Serial.println("ID already used");
    return;
  }*/
  int address=findNextAvailableID();

  Serial.println("Enrolling ID " + String(empID) + ", Name: " + empName);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enroll: ");
  lcd.print(String("ID") + empID);

  lcd.setCursor(0, 1);
  lcd.print(String("Name") + empName);
  delay(3000);

  bool success = false;
  while (!success) {
    Serial.println("Place finger...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Place finger...");
    delay(2000);
    success = enrollFingerprint(address);
    if (!success) {
      Serial.println("Failed, retrying...");
      lcd.setCursor(0, 0);
      lcd.print("Retrying...");
    }
  }

  storeUserInEEPROM(address, empName, empID);

  sendEnrollmentAck(empID);
  Serial.println("Enrollment done for " + empName);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enrolled:");
  lcd.setCursor(0, 1);
  lcd.print(empName);
  delay(2000);
}
void sendEnrollmentAck(int empID) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }
  WiFiClient client;
  HTTPClient http;
  //WiFiClientSecure client;
//  client.setInsecure();  // Accept all certificates (for dev/testing)

 // HTTPClient https;

  const char* ackUrl = "http://testing.indrainsignia.co.in/fingerprint/data.php";
  Serial.println("Connecting to ACK URL: " + String(ackUrl));

  if (!http.begin(client, ackUrl)) {
    Serial.println("Failed to connect to send ack");
    return;
  }

  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
 
  doc["id"] = empID;

  doc["ack"] = "OK";

  String payload;
  serializeJson(doc, payload);

  
  Serial.println(payload);
  String arrpay="[" + payload + "]";
  //Serial.println(arrpay);

 
  
  Serial.println("Sending ACK: " + payload);
//HTTPClient http;
http.begin(client, "http://testing.indrainsignia.co.in/fingerprint/data.php");
http.addHeader("Content-Type", "application/json");
//int httpCode = http.POST(payload);

  delay(100);
  //serializeJson(doc, arrpay);

  Serial.println("Sending ACK: " + payload);
  int httpCode = http.POST(payload);
  Serial.println("HTTP code: " + String(httpCode));

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Server Response: " + response);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ACK sent");
  } else {
    Serial.println("ACK POST failed: " + http.errorToString(httpCode));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ACK failed");
  }

  http.end();
}

//----------------External EEPROM-------------
void writeRecord(uint32_t addr, const String& data) {
  uint8_t devAddr = EEPROM_I2C_ADDRESS | ((addr >> 16) & 0x01);  // For >64KB
  uint16_t memAddr = addr & 0xFFFF;

  Wire.beginTransmission(devAddr);
  Wire.write((memAddr >> 8) & 0xFF);  // MSB
  Wire.write(memAddr & 0xFF);         // LSB

  for (uint8_t i = 0; i < RECORD_SIZE; i++) {
    Wire.write(data[i]);
  }

  Wire.endTransmission();
  delay(10);
}

String readRecord(uint32_t addr) {
  String result = "";

  uint8_t devAddr = EEPROM_I2C_ADDRESS | ((addr >> 16) & 0x01);
  uint16_t memAddr = addr & 0xFFFF;

  Wire.beginTransmission(devAddr);
  Wire.write((memAddr >> 8) & 0xFF);
  Wire.write(memAddr & 0xFF);
  Wire.endTransmission();

  Wire.requestFrom((int)devAddr, RECORD_SIZE);
  while (Wire.available()) {
    char ch = Wire.read();
    result += ch;
  }

  return result;
}

void readAllRecords() {
  uint32_t addr = 0;
  int count = 0;

  while (addr + RECORD_SIZE <= EEPROM_SIZE && count < MAX_RECORDS) {
    String record = readRecord(addr);

    if (record.length() < RECORD_SIZE || record[0] == 0xFF || record[0] == '\0') break;

    String id   = record.substring(0, 6);
    String name = record.substring(6, 14);
    String date = record.substring(14, 24);
    String time = record.substring(24, 32);
    char type   = record.charAt(32);

    Serial.print("Record ");
    Serial.print(count + 1);
    Serial.print(" | ID: "); Serial.print(id);
    Serial.print(" | Name: "); Serial.print(name);
    Serial.print(" | Date: "); Serial.print(date);
    Serial.print(" | Time: "); Serial.print(time);
    Serial.print(" | Type: "); Serial.println(type == 'I' ? "IN" : "OUT");

    addr += RECORD_SIZE;
    count++;
  }

  Serial.print("Total offline records: ");
  Serial.println(count);
}


String makeFixedLengthRecord(String id, String name, String date, String time, char type) {
  id = id.substring(0, 6);   while (id.length() < 6) id += " ";
  name = name.substring(0, 8); while (name.length() < 8) name += " ";
  date = date.substring(0, 10); while (date.length() < 10) date += " ";
  time = time.substring(0, 8); while (time.length() < 8) time += " ";
  return id + name + date + time + type;
}

void syncOfflineDataToServer() {
  uint32_t addr = 0;
  int count = 0;

  Serial.println("Syncing offline records to server...");

  while (addr + RECORD_SIZE <= EEPROM_SIZE && count < MAX_RECORDS) {
    String record = readRecord(addr);

    if (record.length() < RECORD_SIZE || record[0] == 0xFF || record[0] == '\0') break;

    String id   = record.substring(0, 6);
    String name = record.substring(6, 14);
    String date = record.substring(14, 24);
    String time = record.substring(24, 32);
    char type   = record.charAt(32);

    id.trim(); name.trim(); date.trim(); time.trim();

    bool success = false;
    if (type == 'I') {
      Serial.print("Offline Record -> ");
      Serial.print("ID: "); Serial.print(id);
      Serial.print(" | Name: "); Serial.print(name);
      Serial.print(" | Date: "); Serial.print(date);
      Serial.print(" | Time: "); Serial.print(time);
      Serial.print(" | Type: "); Serial.println(type == 'I' ? "IN" : "OUT");

      success = pushOfflineToServer(id, name, date, time, "");  // clock-in
    } else {
      Serial.print("Offline Record -> ");
      Serial.print("ID: "); Serial.print(id);
      Serial.print(" | Name: "); Serial.print(name);
      Serial.print(" | Date: "); Serial.print(date);
      Serial.print(" | Time: "); Serial.print(time);
      Serial.print(" | Type: "); Serial.println(type == 'I' ? "IN" : "OUT");

      success = pushOfflineToServer(id, name, date, "", time);      // clock-out
    }

    if (success) {
      // Overwrite with 0xFF to mark as deleted
      String blank(RECORD_SIZE, (char)0xFF);
      writeRecord(addr, blank);
      count++;
    }

    addr += RECORD_SIZE;
  }

  Serial.print("Synced records: ");
  Serial.println(count);
  currentEEPROMAddress = 0;  // Reset after sync
}

bool pushOfflineToServer(String empID, String name, String dateStr, String inTime, String outTime) {
  HTTPClient https;
  WiFiClientSecure client;
  client.setInsecure();

  String url = "https://testing.indrainsignia.co.in/fingerprint/data.php";  // Your server endpoint
  https.begin(client, url);

  StaticJsonDocument<200> doc;
  if (outTime == "") {
    // Clock-in
    doc["id"] = empID;
    doc["name"] = name;
    doc["date"] = dateStr;
    doc["in"] = inTime;
    //doc["out"] = ;//empty
  } else {
    // Clock-out
    doc["id"] = empID;
    doc["name"] = name;
    doc["date"] = dateStr;
   // doc["in"] = timeStr;
    doc["out"] = outTime;
  }
 /* if (outTime == "") {
    doc["id"] = id;
    doc["name"] = name;
    doc["date"] = dateStr;
    doc["in"] = inTime;
  } else {
    doc["id"] = id;
    doc["out"] = outTime;
  }*/

  String requestBody;
  serializeJson(doc, requestBody);

  https.addHeader("Content-Type", "application/json");
  int httpCode = https.POST(requestBody);

  if (httpCode > 0) {
    String response = https.getString();
    Serial.println("Offline Push ACK: " + response);
    https.end();
    return true;
  } else {
    Serial.println("Offline Push Failed: " + https.errorToString(httpCode));
    https.end();
    return false;
  }
}
////-----------------REA data of exteeprom and display in serial monitor ----------------
void readOfflineRecords() {
  uint32_t addr = 0;
  int count = 0;

  while (addr + RECORD_SIZE <= EEPROM_SIZE && count < MAX_RECORDS) {
    String record = readRecord(addr);

    // Stop if data is blank or invalid
    if (record.length() < RECORD_SIZE || record[0] == 0xFF || record[0] == '\0') {
      break;
    }

    // Extract fields
    String id   = record.substring(0, 6);
    String name = record.substring(6, 14);
    String date = record.substring(14, 24);
    String time = record.substring(24, 32);
    char type   = record.charAt(32);

    id.trim(); name.trim(); date.trim(); time.trim();

    // Print to Serial
    Serial.print("Record ");
    Serial.print(count + 1);
    Serial.print(" | ID: "); Serial.print(id);
    Serial.print(" | Name: "); Serial.print(name);
    Serial.print(" | Date: "); Serial.print(date);
    Serial.print(" | Time: "); Serial.print(time);
    Serial.print(" | Type: "); Serial.println(type == 'I' ? "IN" : "OUT");

    addr += RECORD_SIZE;
    count++;
  }

  Serial.print("Total EEPROM records: ");
  Serial.println(count);
}
//---clear external eeprom--------
void clearExternalEEPROM() {
  Serial.println("Clearing external EEPROM...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Clearing ExtEEPROM");

  for (unsigned long addr = 0; addr < EEPROM_SIZE; addr += PAGE_SIZE) {
    uint8_t block = EEPROM_I2C_BASE_ADDR | ((addr >> 16) & 0x03); // Upper bits set A16 and A17
    uint16_t wordAddr = addr & 0xFFFF;

    Wire.beginTransmission(block);
    Wire.write((wordAddr >> 8) & 0xFF);  // MSB
    Wire.write(wordAddr & 0xFF);         // LSB

    for (uint16_t i = 0; i < PAGE_SIZE; i++) {
      Wire.write(0x00);  // Clear each byte
    }

    if (Wire.endTransmission() != 0) {
      Serial.println("I2C error at block " + String(block) + ", addr " + String(addr));
    }

    delay(10); // Allow EEPROM write cycle to complete

    if (addr % 8192 == 0) {
      Serial.print(".");
    }
  }
 store=false;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ext EEPROM Clear");
  Serial.println("\nExternal EEPROM cleared!");
}


