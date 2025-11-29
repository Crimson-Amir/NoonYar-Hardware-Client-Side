#include "display.h"

// ---------- DISPLAY OBJECTS ----------
LedControl lc(DIN_PIN, CLK_PIN, CS_PIN, 2);
volatile DeviceStatus currentStatus = STATUS_NORMAL;
int num1 = 0;
int num2 = 0;
int num3 = 0;

void displayChar(char c) {
  lc.clearDisplay(0);
  lc.setChar(0, 0, c, false);
}

void displayDash() {
  lc.clearDisplay(0);
  lc.setChar(0, 0, '-', false);
}

// Base pattern: show G segment (dash) on specific digits for all states
static void showBaseGPattern() {
  // Clear both devices first
  lc.clearDisplay(0);
  lc.clearDisplay(1);

  // Device 0: digits 0,2,3,4
  lc.setChar(0, 0, '-', false);
  lc.setChar(0, 2, '-', false);
  lc.setChar(0, 3, '-', false);
  lc.setChar(0, 4, '-', false);

  // Device 1: digits 2,7
  lc.setChar(1, 2, '-', false);
  lc.setChar(1, 7, '-', false);
}

// WiFi connecting pattern
//  - Base G pattern on specified digits
//  - Device 1: 0,1,5 -> 'C'; 3,4,6 -> '1'
static void showWifiConnectingPattern() {
  showBaseGPattern();

  lc.setChar(1, 0, 'C', false);
  lc.setChar(1, 5, 'C', false);
  lc.setChar(1, 1, 'C', false);

  lc.setChar(1, 6, '1', false);
  lc.setChar(1, 4, '1', false);
  lc.setChar(1, 3, '1', false);
}

// MQTT connecting pattern
//  - Base G pattern on specified digits
//  - Device 1: 0,1,5 -> 'C'; 3,4,6 -> '2'
static void showMqttConnectingPattern() {
  showBaseGPattern();

  lc.setChar(1, 0, 'C', false);
  lc.setChar(1, 5, 'C', false);
  lc.setChar(1, 1, 'C', false);

  lc.setChar(1, 6, '2', false);
  lc.setChar(1, 4, '2', false);
  lc.setChar(1, 3, '2', false);
}

// API waiting / init connecting pattern
//  - Base G pattern on specified digits
//  - Device 1: 0,1,5 -> 'C'; 3,4,6 -> '3'
static void showInitPattern() {
  showBaseGPattern();

  lc.setChar(1, 0, 'C', false);
  lc.setChar(1, 5, 'C', false);
  lc.setChar(1, 1, 'C', false);

  lc.setChar(1, 6, '3', false);
  lc.setChar(1, 4, '3', false);
  lc.setChar(1, 3, '3', false);
}

// Error pattern with code:
//  - All digits show '-' (G segment) by default
//  - Device 1 digits 0,5,1 show 'E'
//  - Device 1 digits 6,4,3 show error code ('1','2','3')
static void showErrorCode(char codeChar) {
  // First fill everything with '-'
  for (int dev = 0; dev < 2; dev++) {
    for (int digit = 0; digit < 8; digit++) {
      lc.setChar(dev, digit, '-', false);
    }
  }

  // Left error column (E): 1,0 / 1,5 / 1,1
  lc.setChar(1, 0, 'E', false);
  lc.setChar(1, 5, 'E', false);
  lc.setChar(1, 1, 'E', false);

  // Right error column (code): 1,6 / 1,4 / 1,3
  lc.setChar(1, 6, codeChar, false);
  lc.setChar(1, 4, codeChar, false);
  lc.setChar(1, 3, codeChar, false);
}

void showNumbers(int a, int b, int c) {
  if (currentStatus == STATUS_NORMAL && !confirmationMode) {
    // Only update the main customer digits so we don't disturb cook display on 0,4
    lc.setDigit(0, 0, a % 10, false);
    lc.setDigit(0, 2, b % 10, false);
    lc.setDigit(0, 3, c % 10, false);
  }
}

void showOwnerBreadCounts() {
  int bakerTotal    = bread1_count_baker_display + bread2_count_baker_display + bread3_count_baker_display;
  // Only show baker display when in BAKER mode and there's something to show
  if (displayMode != DISPLAY_MODE_BAKER || bakerTotal <= 0) {
    lc.setRow(1, 6, 0);
    lc.setRow(1, 4, 0);
    lc.setRow(1, 3, 0);
    return;
  }

  // Show baker-display bread counts on device 1 digits 6,4,3
  lc.setDigit(1, 6, bread1_count_baker_display % 10, false);
  lc.setDigit(1, 4, bread2_count_baker_display % 10, false);
  lc.setDigit(1, 3, bread3_count_baker_display % 10, false);
}

void showBakerDisplay() {
  showOwnerBreadCounts();
}

void showDeliveryDisplay() {
  int deliveryTotal = bread1_delivery_display      + bread2_delivery_display      + bread3_delivery_display;

  // Only show delivery display when in DELIVERY mode and there's something to show
  if (displayMode != DISPLAY_MODE_DELIVERY || deliveryTotal <= 0) {
    lc.setRow(1, 0, 0);
    lc.setRow(1, 5, 0);
    lc.setRow(1, 1, 0);
    return;
  }

  // Show delivery-display counts on device 1 digits 0,5,1
  lc.setDigit(1, 0, bread1_delivery_display % 10, false);
  lc.setDigit(1, 5, bread2_delivery_display % 10, false);
  lc.setDigit(1, 1, bread3_delivery_display % 10, false);
}

void showCookDisplay() {
  // Show cook-display counts: 1,2 / 1,7 / 0,4
  if (bread1_cook_display <= 0 && bread2_cook_display <= 0 && bread3_cook_display <= 0) {
    // Nothing to show on cook display: turn digits off
    lc.setRow(1, 2, 0);
    lc.setRow(1, 7, 0);
    lc.setRow(0, 4, 0);
  } else {
    int c1 = bread1_cook_display > 0 ? bread1_cook_display : 0;
    int c2 = bread2_cook_display > 0 ? bread2_cook_display : 0;
    int c3 = bread3_cook_display > 0 ? bread3_cook_display : 0;
    lc.setDigit(1, 2, c1 % 10, false);
    lc.setDigit(1, 7, c2 % 10, false);
    lc.setDigit(0, 4, c3 % 10, false);
  }
}

void setStatus(DeviceStatus st) {
  currentStatus = st;
  Serial.println("New Status: " + String(st));
  lc.clearDisplay(0);

  if (st == STATUS_NORMAL) {
    showNumbers(num1, num2, num3);
    showBakerDisplay();
    showDeliveryDisplay();
    showCookDisplay();
  }
  else if (st == STATUS_WIFI_CONNECTING) {
    showWifiConnectingPattern();
  }
  else if (st == STATUS_MQTT_CONNECTING) {
    showMqttConnectingPattern();
  }
  else if (st == STATUS_WIFI_ERROR) {
    showErrorCode('1');
  }
  else if (st == STATUS_MQTT_ERROR) {
    showErrorCode('2');
  }
  else if (st == STATUS_API_ERROR) {
    showErrorCode('3');
  }
  else if (st == STATUS_INIT || st == STATUS_API_WAITING) {
    showInitPattern();
  }
}
