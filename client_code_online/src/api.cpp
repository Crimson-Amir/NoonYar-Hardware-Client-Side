#include "config.h"
#include "api.h"
#include "network.h"
#include "mqtt.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// ---------- GLOBAL DATA ----------

Preferences prefs;
volatile bool init_success = false;
const char* endpoint_address = "http://cos.voidtrek.com:80/hc";

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
  if (resp.body.isEmpty()) {
    mqttPublishError("api:fetchInitData:failed: empty body (code=" + String(resp.status_code) + ")");
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

  saveInitDataToFlash();
  return true;

}

int apiNewCustomer(const std::vector<int>& breads) {
  StaticJsonDocument<512> bodyDoc;
  bodyDoc["bakery_id"] = atoi(bakery_id);
  JsonObject req = bodyDoc.createNestedObject("bread_requirements");
  for (int i = 0; i < bread_count; ++i) {
    req[String(breads_id[i])] = (i < (int)breads.size() ? breads[i] : 0);
  }
  String body; serializeJson(bodyDoc, body);

  HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/nc"), "PUT", body);
  if (resp.body.isEmpty()) {
    mqttPublishError("api:apiNewCustomer:failed: empty body (code=" + String(resp.status_code) + ")");
    return -1;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, resp.body);
  if (err) { 
    mqttPublishError(String("api:apiNewCustomer:error: ") + err.c_str() + String(" | Body: ") + String(resp.body)); 
    return -1; 
  }

  if (!doc.containsKey("customer_ticket_id")) {
    mqttPublishError("api:apiNewCustomer:missing customer_id" + String(" | Body: ") + String(resp.body));
    return -1;
  }

  return doc["customer_ticket_id"].as<int>();
}

NextTicketResponse apiNextTicket(int customer_ticket_id) {
  NextTicketResponse r;

  StaticJsonDocument<256> bodyDoc;
  bodyDoc["bakery_id"] = atoi(bakery_id);
  bodyDoc["customer_ticket_id"] = customer_ticket_id;
  String body; serializeJson(bodyDoc, body);

  HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/nt"), "PUT", body);

  if (resp.status_code == 400) {
    r.error = "invalid_ticket_number";
    return r;
  }

  if (resp.body.isEmpty()) {
    mqttPublishError("api:apiNextTicket:failed: empty body (code=" + String(resp.status_code) + ")");
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

  HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/ct/" + bakery_id), "GET");
  
  if (resp.status_code == 404) {
    r.error = "empty_queue";
    return r;
  }

  if (resp.body.isEmpty()) {
    mqttPublishError("api:apiCurrentTicket:failed: empty body (code=" + String(resp.status_code) + ")");
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

  HttpResponse resp = sendHttpRequest((String(endpoint_address) + "/st"), "PUT", body);
  if (resp.body.isEmpty()) { 
    mqttPublishError(String("api:apiSkipTicket:failed: response is empty!"));  
    return false; 
  }

  return true;
}
