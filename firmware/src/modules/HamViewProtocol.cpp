#if defined(HAMVIEW_MUI)
#include "HamViewProtocol.h"
#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace HamViewProtocol
{
static void text(JsonVariantConst value, char *out, size_t capacity)
{
    const char *string = value.is<const char *>() ? value.as<const char *>() : "";
    snprintf(out, capacity, "%s", string);
}

bool parseSpot(const char *line, HamViewSpot &spot)
{
    if (!line || strlen(line) > 2048)
        return false;
    JsonDocument document;
    if (deserializeJson(document, line, DeserializationOption::NestingLimit(8)) || !document.is<JsonObject>())
        return false;
    spot = {};
    text(document["fullCallsign"], spot.callsign, sizeof(spot.callsign));
    if (!spot.callsign[0])
        text(document["callsign"], spot.callsign, sizeof(spot.callsign));
    if (!spot.callsign[0])
        text(document["dx"], spot.callsign, sizeof(spot.callsign));
    if (!spot.callsign[0])
        return false;
    JsonVariantConst frequency = document["frequency"].isNull() ? document["freq"] : document["frequency"];
    if (frequency.is<const char *>())
        text(frequency, spot.frequency, sizeof(spot.frequency));
    else if (frequency.is<double>() && isfinite(frequency.as<double>()))
        snprintf(spot.frequency, sizeof(spot.frequency), "%.6g", frequency.as<double>());
    text(document["modeDetail"], spot.mode, sizeof(spot.mode));
    if (!spot.mode[0])
        text(document["mode"], spot.mode, sizeof(spot.mode));
    text(document["spotter"], spot.spotter, sizeof(spot.spotter));
    if (!spot.spotter[0])
        text(document["de"], spot.spotter, sizeof(spot.spotter));
    text(document["comment"], spot.comment, sizeof(spot.comment));
    if (!spot.comment[0])
        text(document["comments"], spot.comment, sizeof(spot.comment));
    if (!spot.comment[0])
        text(document["triggerComment"], spot.comment, sizeof(spot.comment));
    return true;
}

bool containsIgnoreCase(const char *value, const char *needle)
{
    if (!needle || !needle[0])
        return true;
    if (!value)
        return false;
    const size_t length = strlen(needle);
    for (const char *start = value; *start; ++start) {
        size_t i = 0;
        while (i < length && start[i] &&
               tolower(static_cast<unsigned char>(start[i])) == tolower(static_cast<unsigned char>(needle[i])))
            ++i;
        if (i == length)
            return true;
    }
    return false;
}
} // namespace HamViewProtocol
#endif
