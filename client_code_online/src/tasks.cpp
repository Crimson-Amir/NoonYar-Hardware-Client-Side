#include "config.h"
#include "tasks.h"
#include "api.h"
#include "mqtt.h"
#include "mutex.h"
#include "display.h"
#include "network.h"
#include <HardwareSerial.h>

// -----------------------------
// CSN-A2 Printer (UART) Helpers
// -----------------------------

HardwareSerial printerSerial(1); // Use UART1 with custom pins (e.g., RX=16, TX=17)

// Wrapper functions to send ESC/POS commands
template <size_t N>
void sendCommand(const byte (&command)[N]) {
  printerSerial.write(command, N);
}

template <size_t N>
void sendCommand(const byte (&command)[N], byte value) {
  byte modifiedCommand[N];
  for (size_t i = 0; i < N - 1; i++) {
    modifiedCommand[i] = command[i];
  }
  modifiedCommand[N - 1] = value;
  printerSerial.write(modifiedCommand, N);
}

template <size_t N>
void sendCommand(const byte (&command)[N], byte nL, byte nH) {
  byte modifiedCommand[N];
  for (size_t i = 0; i < N - 2; i++) {
    modifiedCommand[i] = command[i];
  }
  modifiedCommand[N - 2] = nL;
  modifiedCommand[N - 1] = nH;
  printerSerial.write(modifiedCommand, N);
}

// Minimal ESC/POS commands we actually use
const byte lineFeed[]          = {10};                  // LF - new line
const byte printAndFeedLines[] = {27, 100, 0};          // ESC d n - print and feed n lines
const byte alignCenter[]       = {27, 97, 1};           // ESC a 1 - center alignment
const byte fontSize[]          = {29, 33, 0};           // GS ! n - font size
const byte boldOn[]            = {27, 69, 1};           // ESC E 1 - bold on
const byte boldOff[]           = {27, 69, 0};           // ESC E 0 - bold off
const byte resetPrinter[]      = {27, 64};              // ESC @ - reset

// Fixed QR Code commands
const byte qrModel[]      = {0x1D, 0x28, 0x6B, 0x04, 0x00, 0x31, 0x41, 0x32, 0x00}; // model 2
const byte qrSize[]       = {0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x43, 0x06};        // size 6
const byte qrErrorLevel[] = {0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x45, 0x31};        // level M
const byte qrPrint[]      = {0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x51, 0x30};        // print

// Print a QR code for the given URL/string
void printQRCode(const char* data) {
  // 1. Model
  sendCommand(qrModel);

  // 2. Size
  sendCommand(qrSize);

  // 3. Error correction level
  sendCommand(qrErrorLevel);

  // 4. Store data
  int dataLength = strlen(data);
  int pL = (dataLength + 3) % 256; // Low byte
  int pH = (dataLength + 3) / 256; // High byte
  byte qrDataHeader[] = {0x1D, 0x28, 0x6B, (byte)pL, (byte)pH, 0x31, 0x50, 0x30};
  sendCommand(qrDataHeader);
  printerSerial.write((const uint8_t*)data, dataLength);

  // 5. Print QR code
  sendCommand(qrPrint);

  // 6. One line gap
  sendCommand(lineFeed);
}

// Initialize printer lazily on first use
void ensurePrinterInitialized() {
  static bool initialized = false;
  if (initialized) {
    return;
  }

  // UART: 19200, 8N1, RX=16, TX=17 (adjust pins to your wiring)
  printerSerial.begin(19200, SERIAL_8N1, 16, 17);
  vTaskDelay(100 / portTICK_PERIOD_MS);

  sendCommand(resetPrinter);
  initialized = true;
}

// Print a simple ticket with customer ID and QR link, no dates
void printCustomerTicket(int bakeryId, int ticketId) {
  ensurePrinterInitialized();

  sendCommand(alignCenter);

  // Small label line
  sendCommand(fontSize, 0x00);
  printerSerial.println("Customer ID");
  sendCommand(lineFeed);

  // Big bold ticket ID
  sendCommand(boldOn);
  sendCommand(fontSize, 0x11); // double width & height (typical ESC/POS)

  char idBuffer[16];
  snprintf(idBuffer, sizeof(idBuffer), "%d", ticketId);
  printerSerial.println(idBuffer);

  sendCommand(boldOff);
  sendCommand(fontSize, 0x00);

  // Some vertical spacing
  sendCommand(printAndFeedLines, 2);

  // QR code with reservation URL
  char urlBuffer[96];
  snprintf(urlBuffer, sizeof(urlBuffer), "https://noonyar.ir/res/?b=%d&t=%d", bakeryId, ticketId);
  printQRCode(urlBuffer);

  // Feed a few lines so the user can tear the ticket
  sendCommand(printAndFeedLines, 3);
}

// -----------------------------
// GM66 Scanner Helpers (UART0)
// -----------------------------

const byte scannerDisableCmd[] = { 0x7E, 0x00, 0x08, 0x01, 0x00, 0xD9, 0xA0, 0xE8, 0x21 };
const byte scannerEnableCmd[]  = { 0x7E, 0x00, 0x08, 0x01, 0x00, 0xD9, 0x00, 0x5D, 0xCB };

void disableScanner() {
  Serial.write(scannerDisableCmd, sizeof(scannerDisableCmd));
}

void enableScanner() {
  Serial.write(scannerEnableCmd, sizeof(scannerEnableCmd));
}

void fetchInitTask(void* param) {
    // Wait for WiFi and MQTT to be fully connected first
    while (!isNetworkReady()) {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    init_success = false;

    // We are now in init phase (after network but before fetchInitData)
    setStatus(STATUS_INIT);

    while (!fetchInitData()) {
        mqttPublishError("tasks:fetchInitTask failed. retrying ...");
        Serial.println("tasks:fetchInitTask failed");
        vTaskDelay(INIT_RETRY_DELAY / portTICK_PERIOD_MS);
    }
    // After basic init, try to restore cook display state from server
    apiInitCookDisplayFromServer();
    setStatus(STATUS_NORMAL);
    init_success = true;
    vTaskDelete(NULL);
}

void newCustomerTask(void* param) {
  if (!isNetworkReadyForApi()) {
    vTaskDelete(NULL);
  }

  if (!tryLockBusy()) {
    vTaskDelete(NULL);
  }

  setStatus(STATUS_API_WAITING);

  std::vector<int> breads(bread_buffer, bread_buffer + bread_count);
  int cid = apiNewCustomer(breads);

  if (cid == -1) {
    mqttPublishError("tasks:newCustomerTask:failed");
    setStatus(STATUS_API_ERROR);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    setStatus(STATUS_NORMAL);
  } else {
    setStatus(STATUS_NORMAL);
  }
  hasCustomerInQueue = true;
  unlockBusy();
  vTaskDelete(NULL);
}

// void ServeTicketTask(void* param) {
//   int ticketId = *(int*)param;
//   delete (int*)param;

//   if (!isNetworkReadyForApi()) {
//     vTaskDelete(NULL);
//   }

//   if (!tryLockBusy()) {
//     vTaskDelete(NULL);
//   }

//   setStatus(STATUS_API_WAITING);
//   ServeTicketResponse r = apiServeTicket(ticketId);
//   bool ok = (r.current_ticket_id != -1);
//   setStatus(ok ? STATUS_NORMAL : STATUS_API_ERROR);
//   if (!ok) mqttPublishError(String("nt:failed:") + r.error);

//   unlockBusy();
//   vTaskDelete(NULL);
// }

// void bakerForceFinish() {
//   int remaining = 0;
//   if (waitDeadline > millis()) {
//     remaining = (waitDeadline - millis()) / 1000;
//   }

//   int sendValue = (remaining > 0) ? -remaining : 0;

//   int* param = new int(sendValue);

//   if (xTaskCreate(sendTimeoutToServerTask, "sendTimeoutToServerTask", 4096, param, 1, NULL) != pdPASS) {
//     mqttPublishError("tasks:bakerForceFinish:sendTimeoutToServerTask:failed");
//     delete param;
//   }

//   waitDeadline = millis();
//   timeForReceiveBread = millis();

//   Serial.println(String("Baker forced finish. Sending: ") + String(sendValue) + " sec");
// }

// void sendTimeoutToServerTask(void* param) {
//     int seconds = *(int*)param;
//     delete (int*)param;

//     bakery_timeout_ms = seconds * 1000UL;
  
//     bool ok = apiUpdateTimeout(seconds);
//     if (!ok) {
//         mqttPublishError("tasks:sendTimeoutToServer:apiUpdateTimeout:failed");
//     } else {
//         Serial.println(String("Timeout sent to server: ") + seconds + " sec");
//     }

//     vTaskDelete(NULL);
// }

// void skipTicketTask(void* param) {
//   int ticketId = *(int*)param;
//   delete (int*)param;

//   if (!isNetworkReadyForApi()) {
//     vTaskDelete(NULL);
//   }

//   bool ok = apiSkipTicket(ticketId);
//   if (!ok) mqttPublishError("tasks:skipTicketTask:failed");

//   vTaskDelete(NULL);
// }

// int calculateCookTime(const CurrentTicketResponse& cur) {
//   int totalTime = 0;
//   for (int i = 0; i < cur.bread_count; i++) {
//     int breadId = cur.breads[i];
//     int count   = cur.bread_counts[i];
//     for (int j = 0; j < bread_count; j++) {
//       if (breads_id[j] == breadId) {
//         totalTime += count * bread_cook_time[j];
//         break;
//       }
//     }
//   }
//   return totalTime;
// }

void ticketFlowTask(void* param) {
  const unsigned long POLL_INTERVAL_NO_CUSTOMER = 300000UL;
  unsigned long lastCheckTime = 0;
  
  while (true) {

      if (!(init_success && isNetworkReadyForApi())) {
        Serial.println("ticketFlowTask:Waiting for init/network...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        continue;
      }
      
      unsigned long now = millis();
      
      if (!hasCustomerInQueue && (now - lastCheckTime < POLL_INTERVAL_NO_CUSTOMER)) {
        Serial.println("ticketFlowTask:no customer in queue and POLL_INTERVAL_NO_CUSTOMER not passed.");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        continue;
      }
      
      Serial.println("ticketFlowTask:hasCustomerInQueue:" + String(hasCustomerInQueue) + "| or time passed");
      CurrentTicketResponse cur = apiCurrentTicket();
      
      lastCheckTime = now;
      Serial.println(String("ticketFlowTask:current_ticket_id: ") + String(cur.current_ticket_id) + " | has_customer_in_queue: " + cur.has_customer_in_queue);

      if (cur.has_customer_in_queue == false) {
        Serial.println("ticketFlowTask:5 second delay");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        continue;
      }

      if (!cur.error.isEmpty() || cur.current_ticket_id < 0) {
        Serial.println("ticketFlowTask:error or no current_ticket_id. 10 sec delay");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        continue;
      }
      if (cur.ready == true){
        // TODO: CALL CUSTOMER 
        Serial.println("ticketFlowTask:breads are ready!");

        currentTicketID = cur.current_ticket_id;
        bool resp = apiSendTicketToWaitList(currentTicketID);
        if (!resp) {mqttPublishError("tasks:ticketFlowTask:apiSendTicketToWaitList reponse is false");}
      }
      else {
        waitDeadline = millis() + (cur.wait_until * 1000UL);
        Serial.println("ticketFlowTask:breads are not ready. wait until" + String(cur.wait_until));
        while (millis() <= waitDeadline) {
          vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
      }
  }
}

void scannerTask(void *pvParameters) {
    while (1) {
        // Only process scans when network and init are ready
        if (!(init_success && isNetworkReady())) {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }

        // Do not accept new scans while a delivery is pending
        if (deliveryPending) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        if (Serial.available()) {
            String qr = Serial.readStringUntil('\n');
            int pos = qr.indexOf("t=");
            if (pos != -1) {
                int ticket_id = qr.substring(pos + 2).toInt();

                Serial.print("Scanned Ticket ID: ");
                Serial.println(ticket_id);
                
                ServeTicketResponse resp = apiServeTicket(ticket_id);
                if (!resp.error.isEmpty()) {
                    if (resp.error == "ticket_is_not_in_wait_list") {
                        Serial.println("ticket_is_not_in_wait_list"); 
                        // BUZZER pattern: 3 short beeps (200ms on, 100ms off)
                        for (int i = 0; i < 3; ++i) {
                            digitalWrite(BUZZER_PIN, HIGH);
                            vTaskDelay(200 / portTICK_PERIOD_MS);
                            digitalWrite(BUZZER_PIN, LOW);
                            if (i < 2) {
                                vTaskDelay(100 / portTICK_PERIOD_MS);
                            }
                        }
                    } else {
                        mqttPublishError("tasks:scannerTask:apiNextTicke reponse failed: " + resp.error);
                    }
                } else {
                    // success
                    Serial.println("success: " + String(resp.bread_counts[0]) + String(resp.bread_counts[1])); 
                    // Directly map ServeTicketResponse bread_counts into delivery display slots
                    bread1_delivery_display = resp.bread_counts[0];
                    bread2_delivery_display = resp.bread_counts[1];
                    bread3_delivery_display = resp.bread_counts[2];

                    // Mark that a delivery is now pending baker confirmation
                    deliveryPending = true;

                    // If nothing is currently shown, switch to delivery mode now
                    if (displayMode == DISPLAY_MODE_NONE) {
                      displayMode = DISPLAY_MODE_DELIVERY;
                    }
                    showDeliveryDisplay();

                    // Disable scanner light/scan while this delivery is pending
                    disableScanner();

                    // BUZZER success pattern: single 300ms beep
                    digitalWrite(BUZZER_PIN, HIGH);
                    vTaskDelay(300 / portTICK_PERIOD_MS);
                    digitalWrite(BUZZER_PIN, LOW);
                }
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void breadButtonsTask(void* param) {
  int rowPins[3] = { ROW1_PIN, ROW2_PIN, ROW3_PIN };
  int colPins[3] = { COL1_PIN, COL2_PIN, COL3_PIN };

  bool buttonState[3][3] = { { false, false, false }, { false, false, false }, { false, false, false } };
  bool lastButtonState[3][3] = { { false, false, false }, { false, false, false }, { false, false, false } };
  unsigned long lastDebounceTime[3][3];
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      lastDebounceTime[r][c] = 0;
    }
  }

  const unsigned long debounceDelay = 50;

  // Init rows as outputs, set HIGH
  for (int i = 0; i < 3; i++) {
    pinMode(rowPins[i], OUTPUT);
    digitalWrite(rowPins[i], HIGH);
  }

  // Init cols as inputs with pullups
  for (int i = 0; i < 3; i++) {
    pinMode(colPins[i], INPUT_PULLUP);
  }

  while (1) {
    // Only respond to buttons when network and init are ready
    if (!(init_success && isNetworkReady())) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    // Scan each row
    for (int row = 0; row < 3; row++) {
      // Set current row LOW, others HIGH
      for (int i = 0; i < 3; i++) {
        digitalWrite(rowPins[i], (i == row) ? LOW : HIGH);
      }

      // Small settle delay
      delayMicroseconds(10);

      // Read all columns
      for (int col = 0; col < 3; col++) {
        bool reading = (digitalRead(colPins[col]) == LOW); // LOW = pressed

        if (reading != lastButtonState[row][col]) {
          lastDebounceTime[row][col] = millis();
        }

        if ((millis() - lastDebounceTime[row][col]) > debounceDelay) {
          if (reading != buttonState[row][col]) {
            buttonState[row][col] = reading;

            if (buttonState[row][col]) {
              Serial.print("Button PRESSED (row,col): ");
              Serial.print(row);
              Serial.print(", ");
              Serial.println(col);
              if (deliveryPending && displayMode == DISPLAY_MODE_DELIVERY && row == 1 && col == 2) {
                // Accept delivery only on Row1, Col2 when delivery is currently shown
                bread1_delivery_display = 0;
                bread2_delivery_display = 0;
                bread3_delivery_display = 0;
                lc.setRow(1, 0, 0);
                lc.setRow(1, 5, 0);
                lc.setRow(1, 1, 0);
                deliveryPending = false;

                // Re-enable scanner once baker has confirmed this delivery
                enableScanner();

                // If there is a pending baker reservation, show it now
                int bakerTotal = bread1_count_baker_display + bread2_count_baker_display + bread3_count_baker_display;
                if (bakerTotal > 0) {
                  displayMode = DISPLAY_MODE_BAKER;
                  showBakerDisplay();
                } else {
                  displayMode = DISPLAY_MODE_NONE;
                  showBakerDisplay();
                }
              } else if (confirmationMode) {
                // In confirmation mode, buttons act as ACCEPT/REJECT only
                if (row == 1 && col == 2) {
                  // Accept (Row2, Col3) -> send order to server
                  confirmationAccepted = true;

                  // Build breads vector mapped from bread1..3 to breads_id
                  std::vector<int> breads;
                  breads.reserve(bread_count);
                  for (int i = 0; i < bread_count; ++i) {
                    if (i == 0)      breads.push_back(bread1_count);
                    else if (i == 1) breads.push_back(bread2_count);
                    else if (i == 2) breads.push_back(bread3_count);
                    else             breads.push_back(0);
                  }

                  // While apiNewCustomer is running, confirmationMode stays true.
                  // uploadInProgress enables baker display animation.
                  uploadInProgress = true;
                  int cid = apiNewCustomer(breads);
                  if (cid == -1) {
                    mqttPublishError("tasks:breadButtonsTask:apiNewCustomer failed");
                    setStatus(STATUS_API_ERROR);
                    // Reset baker display counts on failure
                    bread1_count_baker_display = 0;
                    bread2_count_baker_display = 0;
                    bread3_count_baker_display = 0;
                    showOwnerBreadCounts();
                    uploadInProgress = false;
                    confirmationMode = false;
                    vTaskDelay(5000 / portTICK_PERIOD_MS);
                    setStatus(STATUS_NORMAL);

                    // After finishing (failed) baker confirmation, if a delivery is pending, show it; otherwise clear display mode
                    int deliveryTotal = bread1_delivery_display + bread2_delivery_display + bread3_delivery_display;
                    if (deliveryPending && deliveryTotal > 0) {
                      displayMode = DISPLAY_MODE_DELIVERY;
                      showDeliveryDisplay();
                    } else {
                      displayMode = DISPLAY_MODE_NONE;
                      showBakerDisplay();
                    }
                  } else {
                    // Save current counts before we reset them
                    int c1 = bread1_count;
                    int c2 = bread2_count;
                    int c3 = bread3_count;

                    currentTicketID = cid;

                    // Print ticket for customer with QR code and numeric ID
                    int bakeryIdInt = atoi(bakery_id);
                    printCustomerTicket(bakeryIdInt, currentTicketID);

                    // If API says to show on display, update cook display values
                    if (last_show_on_display) {
                      bread1_cook_display = c1;
                      bread2_cook_display = c2;
                      bread3_cook_display = c3;
                    }

                    // Success: reset bread counts and delivery display, unlock buttons
                    bread1_count = 0;
                    bread2_count = 0;
                    bread3_count = 0;
                    num1 = bread1_count;
                    num2 = bread2_count;
                    num3 = bread3_count;

                    bread1_count_baker_display = 0;
                    bread2_count_baker_display = 0;
                    bread3_count_baker_display = 0;

                    // Clear baker digits (1,6 / 1,4 / 1,3)
                    lc.setRow(1, 6, 0);
                    lc.setRow(1, 4, 0);
                    lc.setRow(1, 3, 0);

                    uploadInProgress = false;
                    confirmationMode = false;
                    setStatus(STATUS_NORMAL);

                    // After successful baker confirmation, if a delivery is pending, show it; otherwise clear display mode
                    int deliveryTotal = bread1_delivery_display + bread2_delivery_display + bread3_delivery_display;
                    if (deliveryPending && deliveryTotal > 0) {
                      displayMode = DISPLAY_MODE_DELIVERY;
                      showDeliveryDisplay();
                    } else {
                      displayMode = DISPLAY_MODE_NONE;
                      showBakerDisplay();
                    }
                  }
                } else if (row == 2 && col == 2) {
                  // Reject (Row3, Col3): clear owner display, reset bread counts and baker displays
                  confirmationAccepted = false;
                  confirmationMode = false;
                  uploadInProgress = false;

                  bread1_count = 0;
                  bread2_count = 0;
                  bread3_count = 0;
                  num1 = bread1_count;
                  num2 = bread2_count;
                  num3 = bread3_count;

                  bread1_count_baker_display = 0;
                  bread2_count_baker_display = 0;
                  bread3_count_baker_display = 0;

                  // Clear owner digits on device 1
                  lc.setRow(1, 6, 0);
                  lc.setRow(1, 4, 0);
                  lc.setRow(1, 3, 0);

                  // Show reset counts on main display (0,0 - 0,2 - 0,3)
                  showNumbers(num1, num2, num3);

                  // After rejecting baker confirmation, if a delivery is pending, show it; otherwise clear display mode
                  int deliveryTotal = bread1_delivery_display + bread2_delivery_display + bread3_delivery_display;
                  if (deliveryPending && deliveryTotal > 0) {
                    displayMode = DISPLAY_MODE_DELIVERY;
                    showDeliveryDisplay();
                  } else {
                    displayMode = DISPLAY_MODE_NONE;
                    showBakerDisplay();
                  }
                }
              } else {
                // Normal bread increment/decrement logic
                int totalBefore = bread1_count + bread2_count + bread3_count;
                int totalAfter = totalBefore;

                if (row == 0 && col == 0) {
                  if (bread1_count > 0) {
                    bread1_count--;
                    totalAfter--;
                  }
                } else if (row == 0 && col == 1) {
                  if (bread1_count < MAX_BREAD_PER_TYPE && totalBefore < max_total_breads) {
                    bread1_count++;
                    totalAfter++;
                  }
                } else if (row == 1 && col == 0) {
                  if (bread2_count > 0) {
                    bread2_count--;
                    totalAfter--;
                  }
                } else if (row == 1 && col == 1) {
                  if (bread2_count < MAX_BREAD_PER_TYPE && totalBefore < max_total_breads) {
                    bread2_count++;
                    totalAfter++;
                  }
                } else if (row == 2 && col == 0) {
                  if (bread3_count > 0) {
                    bread3_count--;
                    totalAfter--;
                  }
                } else if (row == 2 && col == 1) {
                  if (bread3_count < MAX_BREAD_PER_TYPE && totalBefore < max_total_breads) {
                    bread3_count++;
                    totalAfter++;
                  }
                }

                if (totalAfter != totalBefore) {
                  num1 = bread1_count;
                  num2 = bread2_count;
                  num3 = bread3_count;
                  showNumbers(num1, num2, num3);
                }
              }
            } else {
              Serial.print("Button RELEASED (row,col): ");
              Serial.print(row);
              Serial.print(", ");
              Serial.println(col);
            }
          }
        }

        lastButtonState[row][col] = reading;
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}


void confirmButtonTask(void* param) {
  pinMode(CONFIRM_BUTTON_PIN, INPUT_PULLUP);

  int lastReading = HIGH;
  int stableState = HIGH;
  unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 50;

  while (1) {
    // Only allow confirmation when network and init are ready
    if (!(init_success && isNetworkReady())) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    int reading = digitalRead(CONFIRM_BUTTON_PIN);

    // If the reading changed, reset debounce timer
    if (reading != lastReading) {
      lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
      // If the debounced state changed, act on edges
      if (reading != stableState) {
        // Detect button press: HIGH -> LOW
        if (stableState == HIGH && reading == LOW) {
          if (!confirmationMode && currentStatus == STATUS_NORMAL) {
            int totalBread = bread1_count + bread2_count + bread3_count;
            if (totalBread <= 0) {
              Serial.println("Confirm button ignored: all bread counts are zero");
            } else {
              Serial.println("Confirm button pressed -> entering confirmation mode");
              confirmationMode = true;
              confirmationAccepted = false;
              uploadInProgress = false;

              // Copy current bread counts into baker display variables
              bread1_count_baker_display = bread1_count;
              bread2_count_baker_display = bread2_count;
              bread3_count_baker_display = bread3_count;

              // If no display is active, switch to baker mode now
              if (displayMode == DISPLAY_MODE_NONE) {
                displayMode = DISPLAY_MODE_BAKER;
              }
              showBakerDisplay();
            }
          }
        }
        stableState = reading;
      }
    }

    lastReading = reading;
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void confirmAnimationTask(void* param) {
  // Segment animation masks (one segment on at a time)
  const byte segmentMasks[6] = {
    0b01000000, // A
    0b00100000, // B
    0b00010000, // C
    0b00001000, // D
    0b00000100, // E
    0b00000010  // F
  };

  int step = 0;

  while (1) {
    if (confirmationMode && currentStatus == STATUS_NORMAL) {
      byte mask = segmentMasks[step];

      // Always animate customer-facing digits 0,2,3 on device 0
      lc.setRow(0, 0, mask);
      lc.setRow(0, 2, mask);
      lc.setRow(0, 3, mask);

      // Only touch baker-side digits when baker display is the active mode
      if (displayMode == DISPLAY_MODE_BAKER) {
        if (uploadInProgress) {
          // During upload, animate baker display digits 1,6 / 1,4 / 1,3
          lc.setRow(1, 6, mask);
          lc.setRow(1, 4, mask);
          lc.setRow(1, 3, mask);
        } else {
          // Before accept, keep baker display showing static counts
          showOwnerBreadCounts();
        }
      }

      step = (step + 1) % 6;
      vTaskDelay(100 / portTICK_PERIOD_MS);
    } else {
      vTaskDelay(50 / portTICK_PERIOD_MS);
    }
  }
}

void newBreadButtonTask(void* param) {
  pinMode(NEW_BREAD_BUTTON_PIN, INPUT_PULLUP);

  int lastReading = HIGH;
  int stableState = HIGH;
  unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 50;

  while (1) {
    // Only accept new-bread events when network and init are ready
    if (!(init_success && isNetworkReady())) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    int reading = digitalRead(NEW_BREAD_BUTTON_PIN);

    if (reading != lastReading) {
      lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (reading != stableState) {
        // Detect LOW press (HIGH -> LOW): one new bread went to oven
        if (stableState == HIGH && reading == LOW) {
          NewBreadResponse r = apiNewBread();

          if (!r.error.isEmpty()) {
            setStatus(STATUS_API_ERROR);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            setStatus(STATUS_NORMAL);
          } else {
            // Normal case: response has "customer_breads" with counts
            if (r.has_customer_breads &&
                (r.bread_counts[0] > 0 || r.bread_counts[1] > 0 || r.bread_counts[2] > 0)) {
              // Update cook display counts from customer_breads
              bread1_cook_display = r.bread_counts[0];
              bread2_cook_display = r.bread_counts[1];
              bread3_cook_display = r.bread_counts[2];
            } else {
              // No bread left to cook: force '-' on cook display
              bread1_cook_display = -1;
              bread2_cook_display = -1;
              bread3_cook_display = -1;
            }

            setStatus(STATUS_NORMAL);
          }
        }
        stableState = reading;
      }
    }

    lastReading = reading;
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// void upcomingBreadTask(void* param) {
//   const unsigned long POLL_INTERVAL_NO_CUSTOMER = 300000UL;
//   unsigned long lastCheckTime = 0;

//   while (true) {

//       if (!(init_success && isNetworkReadyForApi())) {
//         Serial.println("upcomingBreadTask:Waiting for init/network...");
//         vTaskDelay(5000 / portTICK_PERIOD_MS);
//         continue;
//       }

//       unsigned long now = millis();
      
//       if (!hasUpcomingCustomerInQueue && (now - lastCheckTime < POLL_INTERVAL_NO_CUSTOMER)) {
//         vTaskDelay(10000 / portTICK_PERIOD_MS);
//         continue;
//       }

//       UpcomingCustomerResponse upc = apiUpcomingCustomer();
//       lastCheckTime = now;
//       Serial.println(String("upcomingBreadTask:ready: ") + String(upc.ready));

//       if (!upc.error.isEmpty()) {
//         vTaskDelay(1000 / portTICK_PERIOD_MS);
//         continue;
//       }

//       if (upc.empty_upcoming == true) {
//         vTaskDelay(5000 / portTICK_PERIOD_MS);
//         continue;
//       }

//       if (upc.ready == false) {
//         vTaskDelay(60000 / portTICK_PERIOD_MS);
//         continue;
//       }
      
//       // TODO: SEVEN SEGMENT: SHOW BREADS ON DISPLAY 

//       long int waitTime = millis() + (upc.cook_time_s * 1000UL);

//       while (millis() <= waitTime) {
//         vTaskDelay(5000 / portTICK_PERIOD_MS);
//       }

//       // TODO: SEVEN SEGMENT: CLEAR NUMBER FROM SEVEN SEGMENT

//       vTaskDelay(1000 / portTICK_PERIOD_MS);
//   }
// }
