#include "config.h"
#include "tasks.h"
#include "api.h"
#include "mqtt.h"
#include "mutex.h"
#include "display.h"
#include "network.h"

void fetchInitTask(void* param) {
    while (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    init_success = false;
    setStatus(STATUS_API_WAITING);
    while (!fetchInitData()) {
        mqttPublishError("tasks:fetchInitTask failed. retrying ...");
        vTaskDelay(INIT_RETRY_DELAY / portTICK_PERIOD_MS);
    }
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

  setStatus(cid != -1 ? STATUS_NORMAL : STATUS_API_ERROR);
  if (cid == -1) mqttPublishError("tasks:newCustomerTask:failed");
  hasCustomerInQueue = true;
  unlockBusy();
  vTaskDelete(NULL);
}

void nextTicketTask(void* param) {
  int ticketId = *(int*)param;
  delete (int*)param;

  if (!isNetworkReadyForApi()) {
    vTaskDelete(NULL);
  }

  if (!tryLockBusy()) {
    vTaskDelete(NULL);
  }

  setStatus(STATUS_API_WAITING);
  NextTicketResponse r = apiNextTicket(ticketId);
  bool ok = (r.current_ticket_id != -1);
  setStatus(ok ? STATUS_NORMAL : STATUS_API_ERROR);
  if (!ok) mqttPublishError(String("nt:failed:") + r.error);

  unlockBusy();
  vTaskDelete(NULL);
}

void bakerForceFinish() {
  int remaining = 0;
  if (waitDeadline > millis()) {
    remaining = (waitDeadline - millis()) / 1000;
  }

  int sendValue = (remaining > 0) ? -remaining : 0;  // prevent -0

  int* param = new int(sendValue);

  if (xTaskCreate(updateTimeoutTask, "updateTimeoutTask", 4096, param, 1, NULL) != pdPASS) {
    mqttPublishError("tasks:bakerForceFinish:updateTimeoutTask:failed");
    delete param;
  }

  // expire both deadlines so loops exit naturally
  waitDeadline = millis();
  timeForReceiveBread = millis();

  Serial.println(String("Baker forced finish. Sending: ") + String(sendValue) + " sec");
}

void sendTimeoutToServer(int seconds) {
  int* param = new int(seconds);

  if (xTaskCreate(updateTimeoutTask, "updateTimeoutTask", 4096, param, 1, NULL) != pdPASS) {
    mqttPublishError("tasks:sendTimeoutToServer:updateTimeoutTask:failed");
    delete param;
  }

  Serial.println(String("Timeout sent to server: ") + seconds + " sec");
}

void skipTicketTask(void* param) {
  int ticketId = *(int*)param;
  delete (int*)param;

  if (!isNetworkReadyForApi()) {
    vTaskDelete(NULL);
  }

  bool ok = apiSkipTicket(ticketId);
  if (!ok) mqttPublishError("tasks:skipTicketTask:failed");

  vTaskDelete(NULL);
}

void bakerForceFinish() {
  // calculate remaining waitDeadline
  long remaining = 0;
  if (waitDeadline > millis()) {
    remaining = (waitDeadline - millis()) / 1000; // in seconds
  }

  apiTimeout(remaining);

  waitDeadline = millis();
  timeForReceiveBread = millis();

  Serial.println(String("Baker forced finish. Remaining waitDeadline: ") + remaining + " sec");
}

int calculateCookTime(const CurrentTicketResponse& cur) {
  int totalTime = 0;
  for (int i = 0; i < cur.bread_count; i++) {
    int breadId = cur.breads[i];
    int count   = cur.bread_counts[i];
    for (int j = 0; j < bread_count; j++) {
      if (breads_id[j] == breadId) {
        totalTime += count * bread_cook_time[j];
        break;
      }
    }
  }
  return totalTime;
}

void ticketFlowTask(void* param) {
  
  unsigned long lastCheckTime = 0;
  const unsigned long checkInterval = 300000UL;

  while (true) {
    if (init_success && isNetworkReadyForApi()) {

      unsigned long now = millis();
      if (!hasCustomerInQueue && (now - lastCheckTime < checkInterval)) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        continue;
      }

      CurrentTicketResponse cur = apiCurrentTicket();
      Serial.println(String("ticketFlowTask:current_ticket_id: ") + String(cur.current_ticket_id));
      lastCheckTime = now;

      if (!cur.error.isEmpty() || cur.current_ticket_id < 0) {
        if (cur.error == "empty_queue") {hasCustomerInQueue = false;}
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        continue;
      }

      hasCustomerInQueue = true;
      hasCustomerScanned = false;
      currentTicketID = cur.current_ticket_id;
      int cookTimeSeconds = calculateCookTime(cur);
      
      // TODO: VOICE: NEXT TICKET IN cookTimeSeconds SECOND 

      Serial.println(String("ticketFlowTask:cookTimeSeconds: ") + String(cookTimeSeconds));
      waitDeadline = millis() + (cookTimeSeconds * 1000UL) + bakery_timeout_ms;
      bakery_timeout_ms = 0;
      
      exitWaitTimeout = false;

      while (millis() <= waitDeadline) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
      }

      // TODO: VOICE: TICKET NUMBER XXX
      timeForReceiveBread = millis() + TIME_FOR_RECEIVE_BREAD_MS;
      readyToScan = true;

      while (true) {

        if (millis() >= timeForReceiveBread) {
          Serial.println("deadline finished");
          if (!hasCustomerScanned){
            Serial.println("hsa not scanned. skipping customer ...");
            int* ticketParam = new int(currentTicketID);

            if (xTaskCreate(skipTicketTask, "skipTicketTask", 4096, ticketParam, 1, NULL) != pdPASS) 
              {
                mqttPublishError("tasks:ticketFlowTask:skipTicketTask:failed");
                delete ticketParam;
              }
          }
          break;
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
      } 
      readyToScan = false;
    } else {
        Serial.println("Waiting for init/network...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}

void scannerTask(void *pvParameters) {
    while (1) {
        if (Serial2.available()) {
            String qr = Serial2.readStringUntil('\n');
            int pos = qr.indexOf("t=");
            if (pos != -1) {
                int ticket_id = qr.substring(pos + 2).toInt();

                Serial.print("Scanned Ticket ID: ");
                Serial.println(ticket_id);

                bool is_customer_in_skipped_list = isTicketInSkippedList(ticket_id);
                Serial.println("is_customer_in_skipped_list: " + String(is_customer_in_skipped_list)); 
                if (!is_customer_in_skipped_list && !readyToScan){
                  // showError()
                  Serial.println("customer is not in skipped list and we are not ready to scan"); 
                  vTaskDelay(1000 / portTICK_PERIOD_MS);
                  continue;
                }
                NextTicketResponse resp = apiNextTicket(ticket_id);
                if (!resp.error.isEmpty()) {
                    if (resp.error == "invalid_ticket_number") {
                        Serial.println("invalid_ticket_number"); 
                        // showError();
                    } else {
                        mqttPublishError("tasks:scannerTask:apiNextTicke reponse failed: " + resp.error);
                    }
                } else {
                    // success
                    if (!is_customer_in_skipped_list) {hasCustomerScanned = true;}
                    Serial.println("success: " + String(resp.bread_counts[0]) + String(resp.bread_counts[1])); 
                    // showNumberOnDisplay(resp.current_ticket_id);
                }
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);  // yield to other tasks
    }
}
