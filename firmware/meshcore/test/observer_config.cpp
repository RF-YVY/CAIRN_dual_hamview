// Pins broker validation and topic expansion in CairnObserverConfig.h.
// MQTT publishes cannot use subscription wildcards, and a truncated or unknown
// placeholder must never route observations to an unintended topic. These
// compile-time checks also protect the ability to disable an incomplete setup.
#include "../src/CairnObserverConfig.h"
using namespace CairnObserver;

template <size_t N> constexpr void text(char (&out)[N], const char *value)
{
    size_t i = 0;
    while (i + 1 < N && value[i]) {
        out[i] = value[i];
        ++i;
    }
    out[i] = 0;
}
constexpr bool equal(const char *a, const char *b)
{
    size_t i = 0;
    while (a[i] && a[i] == b[i])
        ++i;
    return a[i] == b[i];
}
constexpr Configuration example()
{
    Configuration c;
    c.enabled = 1;
    text(c.host, "broker.example.net");
    text(c.region, "EU");
    return c;
}
constexpr bool test_blank_defaults()
{
    Configuration c;
    return validConfiguration(c) && !c.enabled && !c.host[0] && !c.region[0] && !c.username[0] && !c.password[0];
}
constexpr bool test_custom_topics_and_tls()
{
    auto c = example();
    c.tls = 1;
    c.port = 8883;
    text(c.topic, "my-org/{region}/nodes/{pubkey}/{type}");
    char out[384]{};
    if (!validConfiguration(c) || !formatTopic(c, "ABC", "packets", out, sizeof(out)) ||
        !equal(out, "my-org/EU/nodes/ABC/packets"))
        return false;
    return formatTopic(c, "ABC", "status", out, sizeof(out)) && equal(out, "my-org/EU/nodes/ABC/status");
}
constexpr bool test_region_optional_for_custom_path()
{
    auto c = example();
    c.region[0] = 0;
    if (validConfiguration(c))
        return false;
    text(c.topic, "organization/receiver/{type}");
    return validConfiguration(c);
}
constexpr bool test_reject_invalid_publish_paths()
{
    auto c = example();
    const char *bad[] = {"mesh/#/{type}",
                         "mesh/+/{type}",
                         "mesh/{unknown}/{type}",
                         "mesh/{type",
                         "mesh/status",
                         "mesh/}/{type}",
                         "{pubkey}/{pubkey}/{pubkey}/{pubkey}/{pubkey}/{pubkey}/{type}"};
    for (const char *path : bad) {
        text(c.topic, path);
        if (validConfiguration(c))
            return false;
    }
    for (size_t i = 0; i < sizeof(c.topic); ++i)
        c.topic[i] = 'x';
    return !validConfiguration(c);
}
constexpr bool test_reject_bad_connection_settings()
{
    auto c = example();
    c.port = 0;
    if (validConfiguration(c))
        return false;
    c.port = 1883;
    text(c.host, "mqtt://broker.example.net/path");
    if (validConfiguration(c))
        return false;
    text(c.host, "broker.example.net");
    text(c.region, "bad/region");
    if (validConfiguration(c))
        return false;
    text(c.region, "EU");
    c.tls = 2;
    return !validConfiguration(c);
}
constexpr bool test_topic_capacity_and_disable()
{
    auto c = example();
    char out[8] = {'a', 'a', 'a', 'a', 'a', 'a', 'a', 'Z'};
    if (formatTopic(c, "ABC", "packets", out, 7) || out[7] != 'Z')
        return false;
    c.enabled = 0;
    c.port = 0;
    c.host[0] = 0;
    text(c.topic, "{unfinished");
    return validConfiguration(c);
}
constexpr bool test_long_credentials()
{
    auto c = example();
    for (size_t i = 0; i + 1 < sizeof(c.username); ++i)
        c.username[i] = 'u';
    for (size_t i = 0; i + 1 < sizeof(c.password); ++i)
        c.password[i] = 'p';
    return validConfiguration(c);
}
static_assert(test_blank_defaults(), "Factory observer settings must remain blank and off");
static_assert(test_custom_topics_and_tls(), "Custom topic placeholders must expand exactly");
static_assert(test_region_optional_for_custom_path(), "Custom brokers may omit a region token");
static_assert(test_reject_invalid_publish_paths(), "Invalid MQTT publish topics must be rejected");
static_assert(test_reject_bad_connection_settings(), "Invalid broker settings must not connect");
static_assert(test_topic_capacity_and_disable(), "Topic bounds and disabling must remain safe");
static_assert(test_long_credentials(), "Long broker credentials must fit without truncation");
