#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "types.h"
#include "network.h"
#include "mqtt.h"
#include "mutex.h"
#include "display.h"
#include "api.h"
#include "tasks.h"

#define BUTTON_PIN 34

void setup()
{
    // Filesystem / GM66 scanner on UART0 (RX0/TX0)
    Serial.begin(9600);
    Serial1.begin(9600, SERIAL_8N1, 35, 32);
    Serial1.setTimeout(25);
    delay(3000);
    LittleFS.begin();

    initPrinter();

    // Display init
    lc.shutdown(0, false);
    lc.clearDisplay(0);
    lc.setIntensity(0, 12);  // Reduced from 15 to minimize EMI/noise between devices
    lc.shutdown(1, false);
    lc.clearDisplay(1);
    lc.setIntensity(1, 12);  // Reduced from 15 to minimize EMI/noise between devices

    // Mutex initialization
    busyMutex = xSemaphoreCreateMutex();
    mqttQueueMutex = xSemaphoreCreateMutex();
    networkBlockMutex = xSemaphoreCreateMutex();

    // WiFi initialization
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    initDisplayEspNow();

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
    digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);
}

void loop()
{
    ensureConnectivity();
    checkDeadlock();
}