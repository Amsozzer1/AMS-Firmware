#pragma once
/*
    A Cluster is a collection of at-most Constants::CLUSTER_SIZE Modules
    Components:
        - TMC2209 - Driver
        - NEMA 17 - Stepper Motor
        - Filament Sensor - undecided

*/
#include "module.h"
#include "constants.h"
#include <ArduinoJson.h>
#include "request.h"
class Cluster
{
    private:
        static inline Module* active {nullptr};
        static inline bool filamentInPrinter {false};
        static void load(Request::request);
        static void unLoad(Request::request);
        static Module* pinToModule(int Slot);
    public:
        static inline int error = 0;
        static bool isAvailable();
        static bool tick();
        static void abort();
        static bool sensedFilamentInPrinter();
        static void setPrinterReport(const char* json, size_t len);
        static size_t currReport (char* out, size_t outLen);
        static void handleCommand(Request::request);
        static inline Module* modules[Constants::CLUSTER_SIZE] {};

        
};
