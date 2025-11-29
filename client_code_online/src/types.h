#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <deque>
#include <Arduino.h>

// ---------- DISPLAY STATE ----------
enum DeviceStatus : uint8_t {
  STATUS_NORMAL,
  STATUS_WIFI_ERROR,
  STATUS_MQTT_ERROR,
  STATUS_API_WAITING,
  STATUS_API_ERROR,
  STATUS_WIFI_CONNECTING,
  STATUS_MQTT_CONNECTING,
  STATUS_INIT
};

// ---------- MQTT MESSAGE STRUCTURE ----------
struct MqttMessage {
  String topic;
  String payload;
  bool retain;
};

// ---------- API RESPONSE STRUCTURES ----------
struct ServeTicketResponse {
  int current_ticket_id = -1;
  bool skipped_customer = false;
  int breads[MAX_KEYS];
  int bread_counts[MAX_KEYS];
  int bread_count = 0;
  String error;
};

struct CurrentTicketResponse {
  bool ready = false;
  int wait_until = -1;
  bool has_customer_in_queue = true;
  int current_ticket_id = -1;
  int breads[MAX_KEYS];
  int bread_counts[MAX_KEYS];
  int bread_count = 0;
  String error;
};

struct NewBreadResponse {
  bool has_customer_breads = false;  // true when "customer_breads" is present
  int bread_index = -1;
  int bread_counts[3] = {0, 0, 0};
  String error;
};

// struct UpcomingCustomerResponse {
//   bool empty_upcoming = false;
//   bool ready = false;
//   int breads[MAX_KEYS];
//   int bread_counts[MAX_KEYS];
//   int bread_count = 0;
//   int cook_time_s = 0;
//
//   String error;
// };


struct HttpResponse {
  int status_code;
  String body;
};

#endif
