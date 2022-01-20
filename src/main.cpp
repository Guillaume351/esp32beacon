#include <WiFiManager.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "soc/rtc_wdt.h"
#include <Arduino_JSON.h>

String serverName = "http://192.168.1.11:6060/";

String beaconId = "beaconchambreg";

int scanTime = 3; //In seconds

WiFiManager wifiManager;

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

      if(advertisedDevice.getRSSI() > maxRssi){
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
      //Serial.println(res);
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
        //http.setTimeout(1); //TODO : remove. Hack to prevent watchdog timeout. TODO : create a tasks, a queue, and run on other core.

        if (httpInitResult == false)
        {
          Serial.println("http.begin() failed!"); //debug
        }

        // Send HTTP GET request
        int httpResponseCode = http.GET();

        if (httpResponseCode > 0)
        {
          // Parse json to get current max RSSI
          String payload = http.getString();
          //Serial.println(payload);
          JSONVar json = JSON.parse(payload);

          maxRssi = json["maxRssi"];

          Serial.printf("HTTP Response code: %d maxRssi: %d\n", httpResponseCode, maxRssi);
        }
        else
        {
          Serial.print("Error code: ");
          Serial.println(httpResponseCode);
          //WiFi.disconnect();
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
  Serial.println(info.disconnected.reason);
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

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);

  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setConnectTimeout(10);
  wifiManager.setConnectRetries(10);
  wifiManager.setConfigPortalTimeout(300); // 5 minutes to setup before reboot

  WiFi.onEvent(WiFiStationDisconnected, SYSTEM_EVENT_STA_DISCONNECTED);

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

  pBLEScan = BLEDevice::getScan(); //create new scan
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