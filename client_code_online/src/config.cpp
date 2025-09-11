#include "config.h"

// ---------- NETWORK CONFIG ----------
const char* ssid       = "iPhone";
const char* password   = "amir1383amir";

const char* bakery_id  = "2";
const char* token      = "sgaG3hYFQAabBvNnYMz8FRH4xAWe9V8nMb9Pu3bDqNo";

const char* mqtt_server = "mqtt.voidtrek.com";
const int   mqtt_port   = 1883;
bool hasCustomerInQueue = true;
volatile int ticketScannedId = -1;

unsigned long lastConnectivityCheck = 0;

