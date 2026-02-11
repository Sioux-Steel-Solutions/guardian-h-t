#ifndef API_H
#define API_H

#include <WebServer.h>
#include "../utils/utils.h"
#include <ArduinoJson.h>


//utility functions go here
void setupApiRoutes(WebServer &server);

#endif