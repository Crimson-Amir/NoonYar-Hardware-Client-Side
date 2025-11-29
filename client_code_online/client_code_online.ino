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
  // Filesystem / GM66 scanner on UART0 (RX0/TX0)
  Serial.begin(9600);
  delay(3000);
  LittleFS.begin();

  // Display init
  lc.shutdown(0, false);
  lc.setIntensity(0, 15);
  lc.clearDisplay(0);
  lc.shutdown(1, false);
  lc.setIntensity(1, 15);
  lc.clearDisplay(1);

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
  xTaskCreatePinnedToCore(ticketFlowTask, "TicketFlow", 8192, NULL, 4, NULL, 0);
  xTaskCreatePinnedToCore(scannerTask, "ScannerTask", 4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(breadButtonsTask, "BreadButtons", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(confirmButtonTask, "ConfirmButton", 2048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(confirmAnimationTask, "ConfirmAnim", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(newBreadButtonTask, "NewBreadButton", 4096, NULL, 2, NULL, 1);
  // xTaskCreatePinnedToCore(upcomingBreadTask, "upcomingBreadTask", 4096, NULL, 2, NULL, 1);

  pinMode(35, INPUT);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

void loop() {
  ensureConnectivity();
  checkDeadlock();
}
