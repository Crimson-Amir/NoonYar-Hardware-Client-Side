#include "config.h"

// ---------- NETWORK CONFIG ----------
const char* ssid       = "Netenza_FDC1D0";
const char* password   = "aA12345!";

const char* bakery_id  = "1";
const char* token      = "mYbfMk4vNpnAt-koMYRX0IptEv8eGfHsYP5FsdVoNmk";

const char* mqtt_server = "77.110.106.199";
const int   mqtt_port   = 1883;
bool hasCustomerInQueue = true;
bool hasCustomerScanned = false;
// bool hasUpcomingCustomerInQueue = true;
volatile int currentTicketID = -1;
bool readyToScan = false;
bool exitWaitTimeout = false;
bool confirmationMode = false;
bool confirmationAccepted = false;
bool uploadInProgress = false;
bool deliveryPending = false;

unsigned long bakery_timeout_ms = 0;
unsigned long lastConnectivityCheck = 0;
unsigned long timeForReceiveBread = 0;
unsigned long waitDeadline = 0;

uint8_t displayEspNowMac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

int bread_count = 0;
int bread_buffer_count = 0;
int bread_buffer[MAX_KEYS];
int breads_id[MAX_KEYS];
int bread_cook_time[MAX_KEYS];

int bread1_count = 0;
int bread2_count = 0;
int bread3_count = 0;

int bread1_count_baker_display = 0;
int bread2_count_baker_display = 0;
int bread3_count_baker_display = 0;

int bread1_delivery_display = 0;
int bread2_delivery_display = 0;
int bread3_delivery_display = 0;

int bread1_cook_display = 0;
int bread2_cook_display = 0;
int bread3_cook_display = 0;

int displayMode = DISPLAY_MODE_NONE;

int max_total_breads = 5;

