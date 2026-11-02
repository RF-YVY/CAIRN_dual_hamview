// Pins actual HamAlert JSON fields and decoding, and rejects truncated or unrelated
// JSON so a quoted key inside a comment cannot become a fabricated alert.
#include "HamViewProtocol.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

int main()
{
    HamViewSpot spot;
    assert(HamViewProtocol::parseSpot(
        R"({"fullCallsign":"N0CALL/P","callsign":"N0CALL","frequency":"14.074","mode":"FT8","comment":"CQ \"DX\" \u00e9"})", spot));
    assert(strcmp(spot.callsign, "N0CALL/P") == 0);
    assert(strcmp(spot.frequency, "14.074") == 0);
    assert(strcmp(spot.comment, "CQ \"DX\" \xc3\xa9") == 0);
    assert(HamViewProtocol::parseSpot(
        R"({"dx":"N0CALL","freq":7.074,"mode":"DIGI","modeDetail":"FT4","de":"TEST","triggerComment":"Wanted DX"})", spot));
    assert(strcmp(spot.frequency, "7.074") == 0 && strcmp(spot.mode, "FT4") == 0);
    assert(strcmp(spot.spotter, "TEST") == 0 && strcmp(spot.comment, "Wanted DX") == 0);
    assert(!HamViewProtocol::parseSpot(R"({"callsign":"N0CALL")", spot));
    assert(!HamViewProtocol::parseSpot(R"({"comment":"\"callsign\":\"FAKE\""})", spot));
    assert(!HamViewProtocol::parseSpot(R"({"callsign":{"value":"N0CALL"}})", spot));
    assert(!HamViewProtocol::parseSpot("[]", spot));
    assert(!HamViewProtocol::parseSpot(std::string(2049, 'x').c_str(), spot));
    assert(HamViewProtocol::containsIgnoreCase("N0CALL/P", "n0call"));
    assert(!HamViewProtocol::containsIgnoreCase("N0CALL", "N0CALL/P"));
    puts("HamView real-format JSON, Unicode, filter, and malformed-input checks passed");
}
