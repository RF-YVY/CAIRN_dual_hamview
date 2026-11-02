#pragma once

#include <Arduino.h>

// Keep background log text out of the MeshCore binary USB stream.
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
