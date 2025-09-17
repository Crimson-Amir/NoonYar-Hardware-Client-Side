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
#define RXD2 16
#define TXD2 17

// ---------- NETWORK CONFIG ----------
extern const char* ssid;
extern const char* password;

extern const char* bakery_id;
extern const char* token;

extern const char* mqtt_server;
extern const int   mqtt_port;
extern bool hasCustomerInQueue;
extern bool hasCustomerScanned;
extern bool readyToScan;
extern bool exitWaitTimeout;
extern volatile int currentTicketID;
extern unsigned long lastConnectivityCheck;
extern unsigned long bakery_timeout_ms;

extern int bread_count;
extern int bread_buffer_count;
extern int bread_buffer[MAX_KEYS];
extern int breads_id[MAX_KEYS];
extern int bread_cook_time[MAX_KEYS];


// ---------- TIMING CONFIG ----------
#define WIFI_RECONNECT_INTERVAL  5000
#define MQTT_RECONNECT_INTERVAL  3000
#define DEADLOCK_TIMEOUT        30000
#define BUSY_TIMEOUT             3000
#define MQTT_QUEUE_TIMEOUT       2000
#define HTTP_TIMEOUT            15000
#define INIT_HTTP_TIMEOUT        7000
#define INIT_RETRY_DELAY         5000
#define HTTP_RETRY_DELAY         2000
#define CONNECTIVITY_CHECK_INTERVAL 2000
#define TIME_FOR_RECEIVE_BREAD_MS 60000

#endif
