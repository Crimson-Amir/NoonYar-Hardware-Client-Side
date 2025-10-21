#include "config.h"
#include "tasks.h"
#include "api.h"
#include "mqtt.h"
#include "mutex.h"
#include "display.h"
#include "network.h"

void fetchInitTask(void* param) {
    // while (WiFi.status() != WL_CONNECTED) {
    //   vTaskDelay(5000 / portTICK_PERIOD_MS);
    // }

    // init_success = false;
    // setStatus(STATUS_API_WAITING);
    // while (!fetchInitData()) {
    //     mqttPublishError("tasks:fetchInitTask failed. retrying ...");
    //     vTaskDelay(INIT_RETRY_DELAY / portTICK_PERIOD_MS);
    // }
    // setStatus(STATUS_NORMAL);
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
        Serial.println("ticketFlowTask:error or not current_ticket_id. 5 sec delay");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        continue;
      }
      if (cur.ready == true){
        // TODO: CALL CUSTOMER 
        Serial.println("ticketFlowTask:breads are ready!");

        currentTicketID = cur.current_ticket_id;
        bool resp = apiSkipTicket(currentTicketID);
        if (!resp) {mqttPublishError("tasks:ticketFlowTask:apiSkipTicket reponse is false");}
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
        if (Serial2.available()) {
            String qr = Serial2.readStringUntil('\n');
            int pos = qr.indexOf("t=");
            if (pos != -1) {
                int ticket_id = qr.substring(pos + 2).toInt();

                Serial.print("Scanned Ticket ID: ");
                Serial.println(ticket_id);
                
                ServeTicketResponse resp = apiServeTicket(ticket_id);
                if (!resp.error.isEmpty()) {
                    if (resp.error == "ticket_is_not_in_skipped_list") {
                        Serial.println("ticket_is_not_in_skipped_list"); 
                        // showError();
                    } else {
                        mqttPublishError("tasks:scannerTask:apiNextTicke reponse failed: " + resp.error);
                    }
                } else {
                    // success
                    Serial.println("success: " + String(resp.bread_counts[0]) + String(resp.bread_counts[1])); 
                    // showNumberOnDisplay(resp.current_ticket_id);
                }
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}


void upcomingBreadTask(void* param) {
  const unsigned long POLL_INTERVAL_NO_CUSTOMER = 300000UL;
  unsigned long lastCheckTime = 0;

  while (true) {

      if (!(init_success && isNetworkReadyForApi())) {
        Serial.println("upcomingBreadTask:Waiting for init/network...");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        continue;
      }

      unsigned long now = millis();
      
      if (!hasUpcomingCustomerInQueue && (now - lastCheckTime < POLL_INTERVAL_NO_CUSTOMER)) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        continue;
      }

      UpcomingCustomerResponse upc = apiUpcomingCustomer();
      lastCheckTime = now;
      Serial.println(String("upcomingBreadTask:ready: ") + String(upc.ready));

      if (!upc.error.isEmpty()) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        continue;
      }

      if (upc.empty_upcoming == true) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        continue;
      }

      if (upc.ready == false) {
        vTaskDelay(60000 / portTICK_PERIOD_MS);
        continue;
      }
      
      // TODO: SEVEN SEGMENT: SHOW BREADS ON DISPLAY 

      long int waitTime = millis() + (upc.cook_time_s * 1000UL);

      while (millis() <= waitTime) {
        vTaskDelay(5000 / portTICK_PERIOD_MS);
      }

      // TODO: SEVEN SEGMENT: CLEAR NUMBER FROM SEVEN SEGMENT

      vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
