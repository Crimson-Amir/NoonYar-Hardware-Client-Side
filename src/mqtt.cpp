#include "mqtt.h"
#include "network.h"
#include "api.h"
#include "mutex.h"
#include "tasks.h"
#include "display.h"
#include <ArduinoJson.h>

// Keep an explicit declaration here so mqtt.cpp remains buildable
// even if header visibility changes in some local build environments.
void markMqttReceiveActivity();

static int parseTicketIdFromJson(const JsonDocument &doc)
{
    if (!doc["ticket_id"].isNull())
    {
        return doc["ticket_id"] | -1;
    }
    if (!doc["customer_ticket_id"].isNull())
    {
        return doc["customer_ticket_id"] | -1;
    }
    return -1;
}

struct IncomingTicketJob
{
    int bakeryId;
    int ticketId;
    String token;
    bool shouldPrint;
    bool shouldDisplay;
};

void incomingTicketTask(void *param)
{
    IncomingTicketJob *job = static_cast<IncomingTicketJob *>(param);

    if (job->shouldPrint)
    {
        printCustomerTicket(job->bakeryId, job->ticketId, job->token);
    }

    if (job->shouldDisplay)
    {
        bool sent = sendCustomerToDisplay(job->ticketId);
        if (!sent)
        {
            mqttPublishError("mqtt:incomingTicketTask:sendCustomerToDisplay failed");
        }
    }

    delete job;
    vTaskDelete(NULL);
}

// ---------- MQTT QUEUE MANAGEMENT ----------
SemaphoreHandle_t mqttQueueMutex;
std::deque<MqttMessage> mqttMessageQueue;

bool queueMqttMessage(const String &topic, const String &payload, bool retain)
{
    if (xSemaphoreTake(mqttQueueMutex, MQTT_QUEUE_TIMEOUT / portTICK_PERIOD_MS) == pdTRUE)
    {
        if (mqttMessageQueue.size() < MAX_MQTT_QUEUE_SIZE)
        {
            MqttMessage msg;
            msg.topic = topic;
            msg.payload = payload;
            msg.retain = retain;
            mqttMessageQueue.push_back(msg);
            xSemaphoreGive(mqttQueueMutex);
            return true;
        }
        xSemaphoreGive(mqttQueueMutex);
    }
    return false;
}

void mqttPublishError(const String &msg)
{
    queueMqttMessage(topic_errors, msg, true);
}

void mqttPublish(const String &topic, const String &payload, bool retain)
{
    queueMqttMessage(topic, payload, retain);
}

void mqttPublishBreadTime(const String &payload)
{
    queueMqttMessage(topic_bread_time, payload, false);
}

int getMqttQueueSize()
{
    int size = 0;
    if (xSemaphoreTake(mqttQueueMutex, 100 / portTICK_PERIOD_MS) == pdTRUE)
    {
        size = mqttMessageQueue.size();
        xSemaphoreGive(mqttQueueMutex);
    }
    return size;
}

void mqttPublisherTask(void *param)
{
    unsigned long lastQueueCheck = 0;
    int queueOverflowCount = 0;

    while (true)
    {
        if (mqtt.connected())
        {
            if (xSemaphoreTake(mqttQueueMutex, 100 / portTICK_PERIOD_MS) == pdTRUE)
            {
                if (!mqttMessageQueue.empty())
                {
                    MqttMessage msg = mqttMessageQueue.front();
                    mqttMessageQueue.pop_front();
                    xSemaphoreGive(mqttQueueMutex);

                    bool published = mqtt.publish(msg.topic.c_str(), msg.payload.c_str(), msg.retain);
                    if (!published)
                    {
                        if (!mqtt.connected())
                        {
                            Serial.println("MQTT disconnected during publish");
                        }
                        else
                        {
                            Serial.println("MQTT publish failed: " + msg.topic + " -> " + msg.payload);
                        }
                    }
                }
                else
                {
                    xSemaphoreGive(mqttQueueMutex);
                }

                if (millis() - lastQueueCheck > 5000)
                {
                    lastQueueCheck = millis();
                    int queueSize = mqttMessageQueue.size();
                    if (queueSize > MAX_MQTT_QUEUE_SIZE * 0.8)
                    {
                        queueOverflowCount++;
                        if (queueOverflowCount > 3)
                        {
                            int clearCount = queueSize / 2;
                            for (int i = 0; i < clearCount; i++)
                            {
                                mqttMessageQueue.pop_front();
                            }
                            queueOverflowCount = 0;
                            Serial.println("MQTT queue overflow - cleared " + String(clearCount) + " messages");
                        }
                    }
                    else
                    {
                        queueOverflowCount = 0;
                    }
                }
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void fetchInitFromMqttTask(void *param)
{
    if (!isNetworkReadyForApi())
    {
        mqttPublishError("init:network_not_ready");
        vTaskDelete(NULL);
    }

    if (!tryLockBusy())
    {
        mqttPublishError("❌ Device busy");
        vTaskDelete(NULL);
    }

    bool ok = fetchInitData();
    if (!ok)
    {
        mqttPublishError("failed");
    }

    unlockBusy();
    vTaskDelete(NULL);
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String payloadStr = "";
    for (unsigned int i = 0; i < length; i++)
    {
        payloadStr += (char)payload[i];
    }

    Serial.println(String("MQTT RX [") + String(topic) + String("]: ") + payloadStr);
    markMqttReceiveActivity();

    // --------- MQTT print + optional display ---------
    if (String(topic) == topic_ticket_job)
    {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payloadStr);
        if (err)
        {
            mqttPublishError(String("mqtt:mqttCallback:ticket_job invalid_json: ") + err.c_str() + String(" | payload=") + payloadStr);
            return;
        }

        int ticketId = parseTicketIdFromJson(doc);
        if (ticketId < 0)
        {
            mqttPublishError(String("mqtt:mqttCallback:ticket_job missing ticket_id | payload=") + payloadStr);
            return;
        }

        String ticketToken = "";
        if (!doc["token"].isNull())
        {
            ticketToken = String(doc["token"].as<const char *>());
        }

        bool shouldPrint = doc["print"] | true;
        bool shouldDisplay = doc["show_on_display"] | true;

        if (shouldPrint && ticketToken.isEmpty())
        {
            mqttPublishError(String("mqtt:mqttCallback:ticket_job missing token for print | payload=") + payloadStr);
            return;
        }

        int bakeryId = doc["bakery_id"] | atoi(bakery_id);

        IncomingTicketJob *job = new IncomingTicketJob();
        job->bakeryId = bakeryId;
        job->ticketId = ticketId;
        job->token = ticketToken;
        job->shouldPrint = shouldPrint;
        job->shouldDisplay = shouldDisplay;

        BaseType_t ok = xTaskCreatePinnedToCore(
            incomingTicketTask,
            "IncomingTicket",
            6144,
            job,
            3,
            NULL,
            1);

        if (ok != pdPASS)
        {
            delete job;
            mqttPublishError("mqtt:mqttCallback:ticket_job failed to create task");
        }
        return;
    }

    // // --------- Update hasUpcomingCustomerInQueue ---------
    // if (String(topic) == topic_upcoming_queue) {
    //     StaticJsonDocument<64> doc;
    //     DeserializationError err = deserializeJson(doc, payloadStr);
    //     if (!err && doc.containsKey("state")) {
    //         hasUpcomingCustomerInQueue = doc["state"] | false;
    //         Serial.println("MQTT update: hasUpcomingCustomerInQueue = " + String(hasUpcomingCustomerInQueue));
    //     } else {
    //         Serial.println("MQTT invalid payload for upcoming queue: " + payloadStr);
    //     }
    //     return;
    // }
}
