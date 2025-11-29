#include "config.h"
#include "api.h"
#include "network.h"
#include "mqtt.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// ---------- GLOBAL DATA ----------

Preferences prefs;
volatile bool init_success = false;
const char* endpoint_address = "http://noonyar.freebyte.shop:80/hc";
bool last_show_on_display = false;

void saveInitDataToFlash() {
  if (!prefs.begin("bakery_data", false)) return;
  prefs.putInt("cnt", bread_count);
  char key[8];
  for (int i = 0; i < bread_count; i++) {
    snprintf(key, sizeof(key), "k%d", i);
    prefs.putInt(key, breads_id[i]);
    snprintf(key, sizeof(key), "v%d", i);
    prefs.putInt(key, bread_cook_time[i]);
  }
  prefs.end();
}


bool fetchInitData() {
  HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/hardware_init?bakery_id=" + bakery_id), "GET", "", INIT_HTTP_TIMEOUT);

  if (resp.body.isEmpty() || resp.status_code != 200) {
    mqttPublishError("api:apiSendTicketToWaitList:failed:empty body or invalid status_code (code=" + String(resp.status_code) + " | body= " + String(resp.body) + ")");
    return false;
  }

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, resp.body);
  if (err) { 
    mqttPublishError(String("api:fetchInitData:error: ") + err.c_str() + String(" | Body: ") + String(resp.body)); 
    return false; 
  }

  bread_count = 0;
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (bread_count >= MAX_KEYS) break;
    breads_id[bread_count] = String(kv.key().c_str()).toInt();
    bread_cook_time[bread_count] = kv.value().as<int>();
    bread_count++;
  }
  Serial.print(String(bread_count));
  saveInitDataToFlash();
  return true;
}

int apiNewCustomer(const std::vector<int>& breads) {
  // Reset flags for this call
  last_show_on_display = false;
  StaticJsonDocument<512> bodyDoc;
  bodyDoc["bakery_id"] = atoi(bakery_id);
  JsonObject req = bodyDoc.createNestedObject("bread_requirements");
  for (int i = 0; i < bread_count; ++i) {
    req[String(breads_id[i])] = (i < (int)breads.size() ? breads[i] : 0);
  }
  String body; serializeJson(bodyDoc, body);

  HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/new_ticket"), "POST", body);
  if (resp.body.isEmpty() || resp.status_code != 200) {
    mqttPublishError("api:apiNewCustomer:failed: empty body or invalid status_code (code=" + String(resp.status_code) + " | body= " + String(resp.body) + ")");
    return -1;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, resp.body);
  if (err) { 
    mqttPublishError(String("api:apiNewCustomer:error: ") + err.c_str() + String(" | Body: ") + String(resp.body)); 
    return -1; 
  }

  if (!doc.containsKey("customer_ticket_id")) {
    mqttPublishError("api:apiNewCustomer:missing customer_ticket_id" + String(" | Body: ") + String(resp.body));
    return -1;
  }

  // Extract extra flag
  last_show_on_display = doc["show_on_display"] | false;

  return doc["customer_ticket_id"].as<int>();
}

NewBreadResponse apiNewBread() {
  NewBreadResponse r;

  HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/new_bread/" + bakery_id), "POST", "");
  if (resp.body.isEmpty() || resp.status_code != 200) {
    mqttPublishError("api:apiNewBread:failed: empty body or invalid status_code (code=" + String(resp.status_code) + " | body= " + String(resp.body) + ")");
    r.error = "http_fail";
    return r;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, resp.body);
  if (err) {
    mqttPublishError(String("api:apiNewBread:error: ") + err.c_str() + String(" | Body: ") + String(resp.body));
    r.error = "json_error";
    return r;
  }

  JsonObject obj = doc.as<JsonObject>();
  if (obj.containsKey("bread_index")) {
    r.bread_index = obj["bread_index"] | -1;
  }

  if (obj.containsKey("customer_breads")) {
    JsonObject breads = obj["customer_breads"].as<JsonObject>();
    r.has_customer_breads = true;
    r.bread_counts[0] = breads["1"] | 0;
    r.bread_counts[1] = breads["2"] | 0;
    r.bread_counts[2] = breads["3"] | 0;
  }

  return r;
}

ServeTicketResponse apiServeTicket(int customer_ticket_id) {
  ServeTicketResponse r;

  StaticJsonDocument<256> bodyDoc;
  bodyDoc["bakery_id"] = atoi(bakery_id);
  bodyDoc["customer_ticket_id"] = customer_ticket_id;
  String body; serializeJson(bodyDoc, body);

  HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/serve_ticket"), "PUT", body);

  if (resp.status_code == 404) {
    r.error = "ticket_is_not_in_wait_list";
    return r;
  }

  if (resp.body.isEmpty() || resp.status_code != 200) {
    mqttPublishError("api:apiNextTicket:failed: empty body or invalid status_code  (code=" + String(resp.status_code) + " | body= " + String(resp.body) + ")");
    r.error = "http_fail";
    return r;
  }

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, resp.body);
  if (err) { 
    mqttPublishError(String("api:apiNextTicket:error:") + err.c_str() + String(" | Body: ") + String(resp.body)); 
    r.error = "json_error";
    return r;
  }
  r.current_ticket_id = doc["current_ticket_id"] | -1;
  r.skipped_customer = doc["skipped_customer"] | false;

  // Server returns bread counts in 'user_detail'
  JsonObject detail = doc["user_detail"].as<JsonObject>();
  r.bread_count = 0;
  for (JsonPair kv : detail) {
    if (r.bread_count < MAX_KEYS) {
      r.breads[r.bread_count] = String(kv.key().c_str()).toInt();
      r.bread_counts[r.bread_count] = kv.value().as<int>();
      r.bread_count++;
    }
  }

  return r;
}

CurrentTicketResponse apiCurrentTicket() {
  CurrentTicketResponse r;

  HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/current_ticket/" + bakery_id), "GET");

  if (resp.body.isEmpty() || resp.status_code != 200) {
    mqttPublishError("api:apiCurrentTicket:failed: empty body or invalid status_code  (code=" + String(resp.status_code) + " | body= " + String(resp.body) + ")");
    r.error = "http_fail";
    return r;
  }

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, resp.body);
  if (err) {
    mqttPublishError(String("api:apiCurrentTicket:error:") + err.c_str() + String(" | Body: ") + String(resp.body)); 
    r.error = "json_error";
    return r;
  }
  
  bool ready = doc["ready"] | false;
  r.ready = ready;
  r.wait_until = doc["wait_until"] | -1;
  r.has_customer_in_queue = doc["has_customer_in_queue"] | true;
  r.current_ticket_id = doc["current_ticket_id"] | -1;
  if (ready == true){
    if (doc.containsKey("current_user_detail") && doc["current_user_detail"].is<JsonObject>()) {
      JsonObject detail = doc["current_user_detail"].as<JsonObject>();
      r.bread_count = 0;
      for (JsonPair kv : detail) {
        if (r.bread_count < MAX_KEYS) {
          r.breads[r.bread_count] = String(kv.key().c_str()).toInt();
          r.bread_counts[r.bread_count] = kv.value().as<int>();
          r.bread_count++;
        }
      }
    }
  }
  return r;
}

bool apiSendTicketToWaitList(int customer_ticket_id) {
  StaticJsonDocument<256> bodyDoc;
  bodyDoc["bakery_id"] = atoi(bakery_id);
  bodyDoc["customer_ticket_id"] = customer_ticket_id;
  String body; serializeJson(bodyDoc, body);

  HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/send_current_ticket_to_wait_list/" + String(bakery_id)), "PUT", body);
  if (resp.body.isEmpty() || resp.status_code != 200) {
    mqttPublishError("api:apiSendTicketToWaitList:failed:empty body or invalid status_code  (code=" + String(resp.status_code) + " | body= " + String(resp.body) + ")");
    return false; 
  }

  return true;
}

// bool isTicketInSkippedList(int customer_ticket_id) {
//   HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/is_ticket_in_skipped_list/" + atoi(bakery_id) + "/" + customer_ticket_id), "GET");
//   if (resp.body.isEmpty()) { 
//     mqttPublishError(String("api:isTicketInSkippedList:failed: empty body (code=" + String(resp.status_code) + ")"));  
//     return false;
//   }

//   StaticJsonDocument<768> doc;
//   DeserializationError err = deserializeJson(doc, resp.body);
//   if (err) {
//     mqttPublishError(String("api:isTicketInSkippedList:error:") + err.c_str() + String(" | Body: ") + String(resp.body)); 
//     return false;
//   }

//   if (!doc.containsKey("is_ticket_in_skipped_list")) {
//     mqttPublishError("api:isTicketInSkippedList:missing key | Body: " + String(resp.body));
//     return false;
//   }

//   return doc["is_ticket_in_skipped_list"].as<bool>();

// }

// bool apiUpdateTimeout(int time_out_minute) {
//   StaticJsonDocument<256> bodyDoc;
//   bodyDoc["bakery_id"] = atoi(bakery_id);
//   bodyDoc["seconds"] = time_out_minute;
//   String body; serializeJson(bodyDoc, body);

//   HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/timeout/update"), "PUT", body);
//   if (resp.body.isEmpty()) { 
//     mqttPublishError(String("api:apiUpdateTimeout:failed: empty body (code=" + String(resp.status_code) + ")"));  
//     return false; 
//   }

//   return true;
// }

// UpcomingCustomerResponse apiUpcomingCustomer() {
//   UpcomingCustomerResponse r;

//   HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/upcoming/" + bakery_id), "GET");

//   if (resp.status_code == 404) {
//     r.error = "empty_upcoming";
//     return r;
//   }

//   if (resp.body.isEmpty()) {
//     mqttPublishError("api:apiUpcomingCustomer:failed: empty body (code=" + String(resp.status_code) + ")");
//     r.error = "http_fail";
//     return r;
//   }

//   StaticJsonDocument<768> doc;
//   DeserializationError err = deserializeJson(doc, resp.body);
//   if (err) {
//     mqttPublishError(String("api:apiUpcomingCustomer:error:") + err.c_str() + String(" | Body: ") + String(resp.body));
//     r.error = "json_error";
//     return r;
//   }

//   r.empty_upcoming = doc["empty_upcoming"] | false;
//   r.ready = doc["ready"] | false;

//   if (r.empty_upcoming == true || r.ready == false) {return r;}

//   r.cook_time_s = doc["cook_time_s"] | 0;
//   if (doc.containsKey("breads")) {
//     JsonObject breadsObj = doc["breads"].as<JsonObject>();
//     r.bread_count = 0;
//     for (JsonPair kv : breadsObj) {
//         if (r.bread_count < MAX_KEYS) {
//             r.breads[r.bread_count]       = String(kv.key().c_str()).toInt();
//             r.bread_counts[r.bread_count] = kv.value().as<int>();
//             r.bread_count++;
//         }
//     }
//   }

//   return r;
// }

