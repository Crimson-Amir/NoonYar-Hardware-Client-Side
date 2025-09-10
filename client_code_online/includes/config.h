#ifndef CONFIG_H
#define CONFIG_H

// ---------- HARDWARE CONFIG ----------
#define MAX_KEYS            10
#define MAX_HTTP_RETRIES    5
#define MAX_MQTT_QUEUE_SIZE 50

// MAX7219 pins
#define DIN_PIN  23
#define CLK_PIN  18
#define CS_PIN   5

// ---------- NETWORK CONFIG ----------
extern const char* ssid;
extern const char* password;

extern const char* bakery_id;
extern const char* token;

extern const char* mqtt_server;
extern const int   mqtt_port;
extern bool hasCustomerInQueue;
extern volatile int ticketScannedId;

// ---------- TIMING CONFIG ----------
#define WIFI_RECONNECT_INTERVAL  3500
#define MQTT_RECONNECT_INTERVAL  2500
#define DEADLOCK_TIMEOUT        30000
#define BUSY_TIMEOUT             3000
#define MQTT_QUEUE_TIMEOUT       1000
#define HTTP_TIMEOUT            15000
#define INIT_HTTP_TIMEOUT        7000
#define INIT_RETRY_DELAY         5000
#define HTTP_RETRY_DELAY          800

#endif
