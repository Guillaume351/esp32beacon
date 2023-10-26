#include <WiFiManager.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "soc/rtc_wdt.h"
#include <Arduino_JSON.h>
#include <EEPROM.h>

String serverName = "";

String beaconId = "";

int scanTime = 3; // In seconds

WiFiManager wifiManager;
WiFiManagerParameter custom_serverIP("server", "Server IP", serverName.c_str(), 40); // 40 is the length
WiFiManagerParameter custom_beaconId("beacon", "Beacon ID", beaconId.c_str(), 40);

BLEScan *pBLEScan;

QueueHandle_t trackQueue;

int maxRssi = -100; // Initial max RSSI value to send a web request for

// Not used currently
struct track
{
  char macAddress[18];
  int rssi;
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    std::string manData = advertisedDevice.getManufacturerData();

    String res((char *)0);
    char *pHex = BLEUtils::buildHexData(nullptr, (uint8_t *)advertisedDevice.getManufacturerData().data(), advertisedDevice.getManufacturerData().length());
    res += pHex;
    free(pHex);

    if (res.startsWith("4c0010050") | res.startsWith("4c0010052")) // Serie 0,1,2,3,4,5 | Serie 6,7
    {
      Serial.printf("Apple Watch RRSI : %d, Advertised Device: %s \n", advertisedDevice.getRSSI(), advertisedDevice.toString().c_str());

      if (advertisedDevice.getRSSI() > maxRssi)
      {
        String serverPath = serverName + "beaconTrack/" + beaconId + "/" + res + "/" + String(advertisedDevice.getRSSI());

        int buff_size = 80;

        // Convert to char array to put it in the queue
        char char_array[buff_size];

        serverPath.toCharArray(char_array, buff_size);

        // Send it in the queue
        xQueueSend(trackQueue, char_array, (TickType_t)0);
      }
    }
    else
    {
      // Serial.println(res);
    }
  }
};

void taskWebRequests(void *pvParameters)
{
  for (;;)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      HTTPClient http;
      char charArrayFromQueue[80];

      // Wait for new track request
      if (xQueueReceive(trackQueue, &(charArrayFromQueue), (TickType_t)50)) // Wait for 50 ticks = 500ms with default tick rate
      {
        // Send request

        String serverPath = charArrayFromQueue;
        // Your Domain name with URL path or IP address with path
        bool httpInitResult = http.begin(serverPath.c_str());
        // http.setTimeout(1); //TODO : remove. Hack to prevent watchdog timeout. TODO : create a tasks, a queue, and run on other core.

        if (httpInitResult == false)
        {
          Serial.println("http.begin() failed!"); // debug
        }

        // Send HTTP GET request
        int httpResponseCode = http.GET();

        if (httpResponseCode > 0)
        {
          // Parse json to get current max RSSI
          String payload = http.getString();
          // Serial.println(payload);
          JSONVar json = JSON.parse(payload);

          maxRssi = json["maxRssi"];

          Serial.printf("HTTP Response code: %d maxRssi: %d\n", httpResponseCode, maxRssi);
        }
        else
        {
          Serial.print("Error code: ");
          Serial.println(httpResponseCode);
          // WiFi.disconnect();
          if (httpResponseCode == -7)
          {
            ESP.restart(); // TODO : fix
          }
        }
        // Free resources
        http.end();
      }
    }
    else
    {
      Serial.println("WiFi Disconnected from BLE func");
    }
  }
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
  Serial.println("Disconnected from WiFi access point");
  Serial.print("WiFi lost connection. Reason: ");
  // Serial.println(info.disconnected.reason);
  Serial.println("Trying to Reconnect");

  if (wifiManager.autoConnect("esp32beacon"))
  {
    Serial.println("Successfully reconnected !");
  }
  else
  {
    ESP.restart();
  }
}

void saveCustomParametersToEEPROMIfNeeded()
{
  if (!serverName.isEmpty())
  {
    // Saving server name to EEPROM
    for (int i = 0; i < serverName.length(); ++i)
    {
      EEPROM.write(i, serverName[i]);
    }
    EEPROM.write(serverName.length(), '\0'); // Null-terminate the string
  }

  if (!beaconId.isEmpty())
  {
    // Saving beacon ID to EEPROM
    int offset = 40 + 1; // Assuming server name length is maximum 40 characters
    for (int i = 0; i < beaconId.length(); ++i)
    {
      EEPROM.write(offset + i, beaconId[i]);
    }
    EEPROM.write(offset + beaconId.length(), '\0'); // Null-terminate the string
  }

  // Commit the data to the EEPROM
  EEPROM.commit();
  EEPROM.end();
}

void loadCustomParametersFromEEPROMIfAvailable()
{
  // Loading server name from EEPROM
  char readChar;
  serverName = "";
  for (int i = 0; i < 40; ++i) // Assuming a maximum length of 40 characters for the server name
  {
    readChar = EEPROM.read(i);
    if (readChar == '\0') // Break if null-terminator is encountered
    {
      break;
    }
    serverName += readChar;
  }

  // Loading beacon ID from EEPROM
  int offset = 40 + 1; // Assuming server name length is maximum 40 characters
  beaconId = "";
  for (int i = 0; i < 40; ++i) // Assuming a maximum length of 40 characters for the beacon ID
  {
    readChar = EEPROM.read(offset + i);
    if (readChar == '\0') // Break if null-terminator is encountered
    {
      break;
    }
    beaconId += readChar;
  }
  EEPROM.end();
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  EEPROM.begin(82);

  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setConnectTimeout(10);
  wifiManager.setConnectRetries(10);
  wifiManager.setConfigPortalTimeout(300); // 5 minutes to setup before reboot

  wifiManager.addParameter(&custom_serverIP);
  wifiManager.addParameter(&custom_beaconId);

  // Load custom parameters from EEPROM first
  loadCustomParametersFromEEPROMIfAvailable();

  // Setup the track queue
  trackQueue = xQueueCreate(10, 80 * sizeof(char));

  // Setup the web request task
  xTaskCreate(
      taskWebRequests,   /* Task function. */
      "taskWebRequests", /* name of task. */
      10000,             /* Stack size of task */
      NULL,              /* parameter of the task */
      1,                 /* priority of the task */
      NULL);             /* Task handle to keep track of created task */

  if (wifiManager.autoConnect("esp32beacon"))
  {
    // If WiFiManager has new values, overwrite the loaded values
    if (String(custom_serverIP.getValue()).length() > 0)
    {
      serverName = custom_serverIP.getValue();
    }

    if (String(custom_beaconId.getValue()).length() > 0)
    {
      beaconId = custom_beaconId.getValue();
    }

    // Save custom parameters to EEPROM if they have been updated
    saveCustomParametersToEEPROMIfNeeded();

    printf("Server IP: %s\n", serverName.c_str());

    HTTPClient http;
    String serverPath = serverName + "registerBeacon/" + beaconId;
    BLEDevice::init("");
    // Your Domain name with URL path or IP address with path
    http.begin(serverPath.c_str());

    // Send HTTP GET request
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      String payload = http.getString();
      Serial.println(payload);
    }
    else
    {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
  else
  {
    Serial.println("Error. Can't setup wifi.");
    ESP.restart();
  }

  pBLEScan = BLEDevice::getScan(); // create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
}

void loop()
{
  Serial.print("Free HeapSize: ");
  Serial.println(esp_get_free_heap_size());
  Serial.println("looping");

  if (WiFi.status() != WL_CONNECTED)
  {

    Serial.println("Wifi disconnected !");
    ESP.restart();
    delay(1000);
  }

  BLEScanResults foundDevices = pBLEScan->start(scanTime);
  Serial.println("Scan done!");
  pBLEScan->clearResults();

  delay(2000);
}