#include "api.h"
#include "network.h"
#include "mqtt.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// ---------- GLOBAL DATA ----------

Preferences prefs;
int breads_id[MAX_KEYS];
int bread_cook_time[MAX_KEYS];
int data_count = 0;
int bread_buffer[MAX_KEYS];
int bread_buffer_count = 0;
volatile bool init_success = false;
const char* endpoint_address = "http://cos.voidtrek.com:80/hc"

void saveInitDataToFlash() {
  if (!prefs.begin("bakery_data", false)) return;
  prefs.putInt("cnt", data_count);
  char key[8];
  for (int i = 0; i < data_count; i++) {
    snprintf(key, sizeof(key), "k%d", i);
    prefs.putInt(key, breads_id[i]);
    snprintf(key, sizeof(key), "v%d", i);
    prefs.putInt(key, bread_cook_time[i]);
  }
  prefs.end();
}

bool fetchInitData() {

  for (int tries = 0; tries < MAX_HTTP_RETRIES; tries++) {
    String resp = sendHttpRequest((String(endpoint_addres) + "/hardware_init?bakery_id=" + bakery_id), "GET", "", INIT_HTTP_TIMEOUT);
    if (resp.isEmpty()) {
      vTaskDelay(HTTP_RETRY_DELAY / portTICK_PERIOD_MS);
      continue;
    }

    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, resp)) {
      mqttPublishError("json_err:init");
      return false;
    }

    data_count = 0;
    for (JsonPair kv : doc.as<JsonObject>()) {
      if (data_count >= MAX_KEYS) break;
      breads_id[data_count] = String(kv.key().c_str()).toInt();
      bread_cook_time[data_count] = kv.value().as<int>();
      data_count++;
    }

    saveInitDataToFlash();
    return true;
  }

  mqttPublishError("init_fetch_failed");
  return false;
}

int apiNewCustomer(const std::vector<int>& breads) {
  StaticJsonDocument<512> bodyDoc;
  bodyDoc["bakery_id"] = atoi(bakery_id);
  JsonObject req = bodyDoc.createNestedObject("bread_requirements");
  for (int i = 0; i < data_count; ++i) {
    req[String(breads_id[i])] = (i < (int)breads.size() ? breads[i] : 0);
  }
  String body; serializeJson(bodyDoc, body);

  String resp = sendHttpRequest((String(endpoint_addres) + "/nc"), "POST", body);
  if (resp.isEmpty()) { 
    mqttPublishError(String("api:apiNewCustomer:failed: response is empty!")); 
    return -1; 
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) { 
    mqttPublishError(String("api:apiNewCustomer:error: ") + err.c_str()); 
    return -1; 
  }

  return doc["customer_id"].as<int>();
}

NextTicketResponse apiNextTicket(int customer_ticket_id) {
  NextTicketResponse r;

  StaticJsonDocument<256> bodyDoc;
  bodyDoc["bakery_id"] = atoi(bakery_id);
  bodyDoc["customer_ticket_id"] = customer_ticket_id;
  String body; serializeJson(bodyDoc, body);

  String resp = sendHttpRequest((String(endpoint_addres) + "/nt"), "PUT", body);
  if (resp.isEmpty()) { 
    mqttPublishError(String("api:apiNextTicket:failed: response is empty!")); 
    r.error = "http_fail"; 
    return r; 
  }

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) { 
    mqttPublishError(String("api:apiNextTicket:error:") + err.c_str()); 
    r.error = "json_error";
    return r;
  }
  r.current_ticket_id = doc["current_ticket_id"] | -1;
  r.skipped_customer = doc["skipped_customer"] | false;

  JsonObject detail = doc["current_user_detail"].as<JsonObject>();
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

  String resp = sendHttpRequest((String(endpoint_addres) + "/ct") + bakery_id, "GET");
  if (resp.isEmpty()) { 
    mqttPublishError(String("api:apiCurrentTicket:failed: response is empty!")); 
    r.error = "http_fail"; 
    return r; 
  }

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, resp);
  if (err) { 
    mqttPublishError(String("api:apiCurrentTicket:error:") + err.c_str()); 
    r.error = "json_error";
    return r;
  }

  r.current_ticket_id = doc["current_ticket_id"] | -1;
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
  return r;
}

bool apiSkipTicket(int customer_ticket_id) {
  StaticJsonDocument<256> bodyDoc;
  bodyDoc["bakery_id"] = atoi(bakery_id);
  bodyDoc["customer_ticket_id"] = customer_ticket_id;
  String body; serializeJson(bodyDoc, body);

  String resp = sendHttpRequest((String(endpoint_addres) + "/st"), "PUT", body);
  if (resp.isEmpty()) { 
    mqttPublishError(String("api:apiSkipTicket:failed: response is empty!"));  
    return false; 
  }

  return true;
}
