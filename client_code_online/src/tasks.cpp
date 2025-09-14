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
  int bread_count = *(int*)param;
  delete (int*)param;

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
  if (cid == -1) mqttPublishError("nc:failed");
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

void currentTicketTask(void* param) {
  if (!isNetworkReadyForApi()) {
    vTaskDelete(NULL);
  }

  CurrentTicketResponse r = apiCurrentTicket();
  bool ok = (r.current_ticket_id != -1);
  if (!ok) mqttPublishError(String("ct:failed:") + r.error);

  vTaskDelete(NULL);
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

int calculateCookTime(const CurrentTicketResponse& cur) {
  int totalTime = 0;
  for (int i = 0; i < cur.bread_count; i++) {
    int breadId = cur.breads[i];
    int count   = cur.bread_counts[i];
    for (int j = 0; j < data_count; j++) {
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
      Serial.println(String(cur.current_ticket_id));
      lastCheckTime = now;

      if (!cur.error.isEmpty() || cur.current_ticket_id < 0) {
        hasCustomerInQueue = false;
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        continue;
      }

      hasCustomerInQueue = true;
      readyToScan = false;
      currentTicketID = cur.current_ticket_id;
      int cookTimeSeconds = calculateCookTime(cur);
      
      // TODO: VOICE: NEXT TICKET IN cookTimeSeconds SECOND 

      Serial.println(String(cookTimeSeconds));
      unsigned long waitDeadline = millis() + (cookTimeSeconds * 1000UL) + bakery_timeout_ms;
      bakery_timeout_ms = 0;
      
      exitWaitTimeout = false;
      while (!exitWaitTimeout) {
        if (millis() >= waitDeadline) {
          exitWaitTimeout = true;
          Serial.println("Initial wait period finished.");
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
      }

      // TODO: VOICE: TICKET NUMBER XXX
      unsigned long timeForReceiveBread = millis() + TIME_FOR_RECEIVE_BREAD_MS;
      bool processed = false;
      readyToScan = true;

      while (!processed) {

        if (millis() >= timeForReceiveBread) {
          Serial.println("deadline finished");
          int* ticketParam = new int(currentTicketID);

          if (xTaskCreate(skipTicketTask, "skipTicketTask", 4096, ticketParam, 1, NULL) != pdPASS) 
          {
            mqttPublishError("tasks:ticketFlowTask:skipTicketTask:failed");
            delete ticketParam;
          }

          processed = true;
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
      } 
    } else {
        Serial.println("Waiting for init/network...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
  }
}