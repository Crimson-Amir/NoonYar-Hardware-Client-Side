  #include <LittleFS.h>
  #include "src/config.h"
  #include "src/types.h"
  #include "src/network.h"
  #include "src/mqtt.h"
  #include "src/mutex.h"
  #include "src/display.h"
  #include "src/api.h"
  #include "src/tasks.h"

#define BUTTON_PIN 34

void setup() {
  // Filesystem
  Serial.begin(115200);
  delay(1000);
  LittleFS.begin();

  // Display init
  lc.shutdown(0, false);
  lc.setIntensity(0, 8);
  lc.clearDisplay(0);

  // Mutex initialization
  busyMutex = xSemaphoreCreateMutex();
  mqttQueueMutex = xSemaphoreCreateMutex();
  networkBlockMutex = xSemaphoreCreateMutex();

  // WiFi initialization
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // MQTT initialization
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);

  // Start tasks
  xTaskCreatePinnedToCore(mqttPublisherTask, "MqttPublisher", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(fetchInitTask, "InitFetchBoot", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(ticketFlowTask, "TicketFlow", 8192, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(scannerTask, "ScannerTask", 4096, NULL, 1, NULL, 1);

  showNumbers(num1, num2, num3);

  pinMode(BUTTON_PIN, INPUT);
}

void loop() {
  ensureConnectivity();
  checkDeadlock();

  if (digitalRead(BUTTON_PIN) == HIGH) {
    Serial.println("Button pressed! Adding new customer...");

    // Example: update bread_buffer (here just fill with demo values)
    bread_buffer[0] = 3;
    bread_buffer[1] = 2;

    // Start task (no param, since we use global bread_count)
    xTaskCreate(
      newCustomerTask,     // function
      "NewCustomerTask",   // task name
      4096,                // stack size
      NULL,                // param (unused now)
      1,                   // priority
      NULL                 // task handle
    );
    delay(5000);
    }
}
