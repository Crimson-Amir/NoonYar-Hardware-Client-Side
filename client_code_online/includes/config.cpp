#include "config.h"

// ---------- NETWORK CONFIG ----------
const char* ssid       = "Netenza_FDC1D0";
const char* password   = "aA12345!";

const char* bakery_id  = "1";
const char* token      = "UQ0IYlmGWJbn-myt2sZtMKqgKSVBjGx18tGLxaB4aNs";

const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;
bool hasCustomerInQueue = true;
volatile int ticketScannedId = -1;
