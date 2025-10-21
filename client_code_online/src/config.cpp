#include "config.h"

// ---------- NETWORK CONFIG ----------
const char* ssid       = "iPhone";
const char* password   = "amir1383amir";

const char* bakery_id  = "3";
const char* token      = "O-FWYGZGAsCrsU7i0FQoNc1j_Mf_Klnxb_zwtDXR7Uo";

const char* mqtt_server = "77.110.106.199";
const int   mqtt_port   = 1883;
bool hasCustomerInQueue = true;
bool hasCustomerScanned = false;
bool hasUpcomingCustomerInQueue = true;
volatile int currentTicketID = -1;
bool readyToScan = false;
bool exitWaitTimeout = false;

unsigned long bakery_timeout_ms = 0;
unsigned long lastConnectivityCheck = 0;
unsigned long timeForReceiveBread = 0;
unsigned long waitDeadline = 0;

int bread_count = 0;
int bread_buffer_count = 0;
int bread_buffer[MAX_KEYS];
int breads_id[MAX_KEYS];
int bread_cook_time[MAX_KEYS];

