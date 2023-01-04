#include "sdusb.h"
#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <SD.h>
#include "Freenove_WS2812_Lib_for_ESP32.h"


// SD Card
#define SD_MISO  37
#define SD_MOSI  35
#define SD_SCK   36
#define SD_CS    34
SDCard2USB dev;

// Networking
const char* ssid = "WIFI_SSID";				//-- REPLACE WITH YOUR VALUES
const char* password = "WIFI_PASSWORD"; 	//-- REPLACE WITH YOUR VALUES
const char* mqtt_server = "MQTT_IP"; 		//-- REPLACE WITH YOUR VALUES
const char* mqtt_cname = "froniusclient";
const char* mqtt_user = "MQTT_USER";		//-- REPLACE WITH YOUR VALUES
const char* mqtt_pw = "MQTT_PW";			//-- REPLACE WITH YOUR VALUES
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

// Program Variables
#define FRONIUS_LOG "/SYMO/01/DATA.CSV"
long last_log_size = 0;

// --  Inverter Data
int currentDay = 0;
float energyToday = 0.0;
float currentVolts = 0.0;
float currentAmps = 0.0;
long logPeriod = 0.0;

// LED BUILT-IN
#define LEDS_COUNT  1 
#define LEDS_PIN  9
#define CHANNEL   0
Freenove_ESP32_WS2812 strip = Freenove_ESP32_WS2812(LEDS_COUNT, LEDS_PIN, CHANNEL, TYPE_GRB);


void setup()
{
  Serial.begin(115200);

  // Start led
  strip.begin();
  strip.setBrightness(10);  
  strip.setLedColorData(0, 255, 0, 0); // RED
  strip.show();

  // Initialize USB Disk
  if (dev.initSD(SD_SCK, SD_MISO, SD_MOSI, SD_CS))
  {
    if (dev.begin()) {
      Serial.println("MSC lun 1 begin");
    } else log_e("LUN 1 failed");
  } else Serial.println("Failed to init SD");

  // Clear SD Card at boot because all is broken and we need a clear start
  //Serial.println("Clearing SD Card");
  //clearSD();

  // Init data from eeprom
  EEPROM.begin(2);
  currentDay = EEPROM.read(0);
  energyToday = EEPROM.read(1);
  EEPROM.end();

  // Setup Wifi
  strip.setLedColorData(0, 0, 0, 255); // BLUE
  strip.show();
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Set online state to mqtt
  // Wifi Stuff
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  client.publish("espfronius/state", "online"); // Set state to online
  strip.setLedColorData(0, 0, 255, 0); // Green
  strip.show();

  delay(1000);
}

void loop()
{
  // Wifi Stuff
  if (!client.connected()) {
    reconnect();
    strip.setLedColorData(0, 0, 0, 255); // Blue
    strip.show();
  }
  client.loop();


  // Main program check every 10s
  long now = millis();
  if (now - lastMsg > 10000) {
    // Deactivate led
    strip.setLedColorData(0, 255, 255, 0); // Yellow
    strip.show();

    lastMsg = now;

    // Check if file exists
    if (!SD.exists(FRONIUS_LOG)) {
      Serial.println("Cannot find log file");
      return;
    }

    // Open file
    File file = SD.open(FRONIUS_LOG);
    if (!file) {
      Serial.println("Failed to open file for reading");
      return;
    }
    // Check if something changed
    if (last_log_size == file.size()) {
      file.close();
      return;
    }
    Serial.println("File changed");

    // Check if file is too big
    // file.read only addresses uint16 -> max. 32767 Chars per file
    if (file.size() > 32000) {
      // Clear file
      Serial.println("File too big -> clearing");
      file.close();
      SD.remove(FRONIUS_LOG);
      SD.open(FRONIUS_LOG, FILE_WRITE);
      last_log_size = 0;
      return;
    }

    // First boot
    if (last_log_size == 0) {
      Serial.print("- Set first boot file size to ");
      last_log_size = file.size();
      Serial.println(last_log_size);
    }

    // Skip to approximate EOF
    Serial.println("Searching EOF");
    uint16_t aeof;
    if (last_log_size < 800) {
      aeof = last_log_size;
    } else {
      aeof = last_log_size - 500;
    }
    if (!file.seek(aeof)) {
      Serial.print("- Seek failed! @ ");
      Serial.println(aeof);
      file.close();
      return;
    }
    Serial.print("- Seek success to ");
    Serial.println(aeof);

    String ll;
    // Seek last line written
    while (file.available()) {
      ll = file.readStringUntil('\n');
      Serial.println(ll);
    }
    Serial.print("- Last Line @ ");
    Serial.println(file.position());
    // Save new filesize
    last_log_size = file.position();
    file.close();
    // Print line
    Serial.print("[");
    Serial.print(ll);
    Serial.println("]");

    if (ll.length() < 30) {
      Serial.println("-- Empty line");
      return;
    }
    if (ll.length() > 300) {
      Serial.println("- Line too long!");
      return;
    }

    if (!parseLine(ll)) {
      Serial.println("- Parse Failed");
      return;
    }

    Serial.print("Current Day: ");
    Serial.println(currentDay);
    Serial.print("Energy produced today: ");
    Serial.println(energyToday);
    Serial.print("DC Volts: ");
    Serial.println(currentVolts);
    Serial.print("DC Amps: ");
    Serial.println(currentAmps);
    Serial.print("DC Power: ");
    Serial.println(currentAmps * currentVolts);
    Serial.print("Log Intervall: ");
    Serial.println(logPeriod);

    Serial.println("- Publishing to MQTT");
    publishToMQTT();
    strip.setLedColorData(0, 0, 255, 0); // Green
    strip.show();
  }


  delay(5000);
}

void publishToMQTT() {
  pub("espfronius/energy", String(energyToday/3600/1000)); // Map Ws to kWh
  pub("espfronius/dcvolts", String(currentVolts));
  pub("espfronius/dcamps", String(currentAmps));
  pub("espfronius/dcwatts", String(currentVolts * currentAmps));
}

void pub(char* topic, String str) {
  int str_len = str.length() + 1;
  char char_array[str_len];
  str.toCharArray(char_array, str_len);
  client.publish(topic, char_array);
}

void clearSD() {
  // Well at least i tried :/
  if (SD.exists("/SYMO/01/DATA.CSV")) {
    if (SD.remove("/SYMO/01/DATA.CSV"))
      Serial.println("- /SYMO/01/DATA.CSV Failed");
  }
  if (SD.exists("/SYMO/01/DALO.FLD")) {
    if (SD.remove("/SYMO/01/DALO.FLD"))
      Serial.println("- /SYMO/01/DDALO.FLD Failed");
  }
  if (SD.exists("/SYMO/01/EVENT.CSV")) {
    if (SD.remove("/SYMO/01/EVENT.CSV"))
      Serial.println("- /SYMO/01/EVENT.CSV Failed");
  }
  if (SD.exists("/SYMO/01/FRONIUS.SYS")) {
    if (SD.remove("/SYMO/01/FRONIUS.SYS"))
      Serial.println("- /SYMO/01/FRONIUS.SYS Failed");
  }
  if (SD.exists("/SYMO/01")) {
    if (SD.rmdir("/SYMO/01"))
      Serial.println("- /SYMO/01/ Failed");
  }
  if (SD.exists("/SYMO")) {
    if (SD.rmdir("/SYMO"))
      Serial.println("- /SYMO/ Failed");
  }
}

bool parseLine(String line) {
  Serial.println("- Parsing line");
  char arr[255];
  memset(arr, 0, sizeof(arr));
  uint8_t i = 0, k = 0;
  int cd;
  for (uint8_t j = 0; j < line.length(); j++) {
    if (line.charAt(j) == ';') {
      String rc(arr);
      Serial.println(rc);
      // Extract fields
      switch (i) {
        case 0: // Day
          cd = parseDay(rc);
          if (currentDay != cd) {
            Serial.println("-- New day");
            currentDay = cd;
            // Save to eeprom
            EEPROM.begin(2);
            EEPROM.write(0, currentDay); // Save current day
            EEPROM.write(1, 0); // Reset daily energy production
            EEPROM.end();
            energyToday = 0.0;
          }
          break;
        case 4: // LogPeriod
          logPeriod = parseSN(rc);
          if (logPeriod == 0) {
            return false;
          }
          break;
        case 5: // Energy produced during logperiod
          energyToday += parseSN(rc);
          // Save to eeprom
          EEPROM.begin(2);
          EEPROM.write(1, energyToday); // Save daily energy production
          EEPROM.end();
          break;
        case 14: // Solar Volts
          currentVolts = parseSN(rc);
          break;
        case 15: // Solar Amps
          currentAmps = parseSN(rc);
          break;
      }
      i++;
      k = 0;
      memset(arr, 0, sizeof(arr));
      //Serial.println();
    } else {
      arr[k] = line.charAt(j);
      k++;
    }
  }
  Serial.println("- Parsing finished");
  return true;
}

int parseDay(String line) {
  return line.substring(line.lastIndexOf('-') + 1).toInt();
}

double parseSN(String num) {
  if (num.indexOf('e') == -1) {
    return num.toDouble();
  }
  double d = (num.substring(0, num.indexOf('e'))).toDouble();
  if (num.indexOf('-') != -1) {
    // negative potency
    uint8_t p = (num.substring(num.indexOf('-') + 1)).toInt();
    d = d / pow(10, p);
  } else if (num.indexOf('+') != -1) {
    // positive potency
    uint8_t p = (num.substring(num.indexOf('+') + 1)).toInt();
    d = d * pow(10, p);
  }
  return d;
}

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;

  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_cname, mqtt_user, mqtt_pw)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
