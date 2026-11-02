#pragma once
#include <stddef.h>
#include <stdint.h>
namespace CairnMap
{
constexpr unsigned TILES = 6;
bool begin();
void close();
void request(unsigned index, unsigned zoom, int x, int y);
bool poll(unsigned &index);
const uint8_t *data(unsigned index);
size_t size(unsigned index);
const char *status();
const char *provider();
const char *attribution();
bool saveProvider(const char *url, const char *credit);
void project(double latitude, double longitude, unsigned zoom, double &x, double &y);
void unproject(double x, double y, unsigned zoom, double &latitude, double &longitude);
} // namespace CairnMap
