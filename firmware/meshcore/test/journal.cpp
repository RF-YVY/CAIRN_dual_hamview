// Exercises the production journal across restarts, circular overwrite, delivery updates,
// and a torn write; corrupt records must never erase the remaining message history.
#include "CairnJournal.h"
#include "SPIFFS.h"
#include <cassert>
#include <cstdio>

int main()
{
    CairnJournal first;
    assert(first.begin() && first.healthy() && first.count() == 0);
    uint8_t peer[32] = {17};
    auto incoming = first.add("Peer", "Received message", 1800000000, peer, 255, false);
    auto outgoing = first.add("Peer", "Sent message", 1800000001, peer, 255, true);
    assert(incoming && outgoing && outgoing != incoming);
    assert(first.update(outgoing, CairnJournal::Sent, 0x12345678));
    first.acknowledge(0);
    assert(first.at(0)->status == CairnJournal::Sent);
    first.acknowledge(0x12345678);
    assert(first.at(0)->status == CairnJournal::Delivered);
    CairnJournal restarted;
    assert(restarted.begin() && restarted.count() == 2);
    assert(restarted.at(0)->id == outgoing && restarted.at(0)->status == CairnJournal::Delivered);
    assert(restarted.at(1)->id == incoming && strcmp(restarted.at(1)->text, "Received message") == 0);
    for (unsigned i = 0; i < 300; ++i) {
        char text[32];
        snprintf(text, sizeof(text), "Channel message %u", i);
        assert(restarted.add("Public", text, 1800000010 + i, nullptr, 0, false));
    }
    assert(disk.size() == 128 * sizeof(CairnMessage));
    CairnJournal wrapped;
    assert(wrapped.begin() && wrapped.count() == 128);
    assert(strcmp(wrapped.at(0)->text, "Channel message 299") == 0);
    assert(strcmp(wrapped.at(127)->text, "Channel message 172") == 0);
    nextWriteLimit = 11;
    wrapped.add("Public", "Interrupted write", 1800000400, nullptr, 0, false);
    assert(!wrapped.healthy());
    CairnJournal recovered;
    assert(recovered.begin() && recovered.count() == 127);
    assert(strcmp(recovered.at(0)->text, "Channel message 299") == 0);
    auto restored = recovered.add("Public", "After recovery", 1800000401, nullptr, 0, false);
    assert(restored && recovered.healthy());
    CairnJournal finalRestart;
    assert(finalRestart.begin() && finalRestart.at(0)->id == restored);
    assert(strcmp(finalRestart.at(0)->text, "After recovery") == 0);
    puts("Journal restart, ACK, ring overwrite, and torn-write recovery checks passed");
}
