#include <WiFiManager.h>  
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

String serverName = "http://192.168.0.93:6060/";

String beaconId = "beaconchambreg";

int scanTime = 3; //In seconds

WiFiManager wifiManager;

BLEScan *pBLEScan;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {

    std::string manData = advertisedDevice.getManufacturerData();

    String res = "";

    char *pHex = BLEUtils::buildHexData(nullptr, (uint8_t *)advertisedDevice.getManufacturerData().data(), advertisedDevice.getManufacturerData().length());
    res += pHex;
    free(pHex);

    if (res.startsWith("4c0010050")) //if (res.startsWith("4c0010050"))
    {
     // Serial.println("Apple Watch Found! " + String(advertisedDevice.getRSSI()  + advertisedDevice.toString()));
      Serial.printf("Apple Watch RRSI : %d, Advertised Device: %s \n", advertisedDevice.getRSSI(),advertisedDevice.toString().c_str());

      if (WiFi.status() == WL_CONNECTED)
      {
        HTTPClient http;

        String serverPath = serverName + "beaconTrack/" + beaconId + "/" + res + "/" + String(advertisedDevice.getRSSI());

        // Your Domain name with URL path or IP address with path
        http.begin(serverPath.c_str());
        http.setTimeout(1);

        // Send HTTP GET request
        int httpResponseCode = http.GET();

        if (false)//httpResponseCode > 0)
        {
          Serial.print("HTTP Response code: ");
          Serial.println(httpResponseCode);
          //String payload = http.getString();
          //Serial.println(payload);
        }
        else
        {
          //Serial.print("Error code: ");
          //Serial.println(httpResponseCode);
          //WiFi.disconnect();
        }
        // Free resources
        http.end();
      }
      else
      {
        Serial.println("WiFi Disconnected from BLE func");
      }
    }
    else
    {
      //Serial.println(res);
    }

    //Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
  }
};

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Disconnected from WiFi access point");
  Serial.print("WiFi lost connection. Reason: ");
  Serial.println(info.disconnected.reason);
  Serial.println("Trying to Reconnect");
  

    if (wifiManager.autoConnect("esp32beacon")){
      Serial.println("Successfully reconnected !");
    } else {
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

  if (wifiManager.autoConnect("esp32beacon")){
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

  if (WiFi.status()!=WL_CONNECTED)
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