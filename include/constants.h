#pragma once
#include "topicRegistry.hpp"

#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #error "Copy include/secrets.example.h to include/secrets.h and fill in your network."
#endif

class Constants {
    public:
        static inline const char* MQTT_ID { TopicRegistry::macStr() };
        static inline constexpr int BUFFER_SIZE {512};

        // @TODO: Config ?
        static inline constexpr int CLUSTER_SIZE{ 8 };
        static inline constexpr int LOAD_TIMEOUT_MS { 5000 };
        static inline constexpr int ENGAGE_TIMEOUT_MS { 5000 };
        static inline constexpr int PLUS_FREQUENCY_MS { 500 };
        static inline constexpr int STEPS_PER_TICK { 100 };
        static inline constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS { 15000 };
        static inline constexpr unsigned long MQTT_RECONNECT_BACKOFF_MS { 5000 };
        static inline constexpr unsigned long PUBLISH_INTERVAL_MS { 5000 };

        static inline constexpr const char* ssid { WIFI_SSID };
        static inline constexpr const char* pass { WIFI_PASS };
        static inline constexpr const char* MQTT_HOST { MQTT_BROKER_HOST };
        static inline constexpr int MQTT_PORT { MQTT_BROKER_PORT };
};
