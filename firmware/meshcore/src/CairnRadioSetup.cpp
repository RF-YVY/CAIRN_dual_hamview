#include "CairnRadioSetup.h"
#if defined(CAIRN_PUBLIC_RELEASE)
#include <Preferences.h>
#endif

namespace CairnRadioSetup
{
#if defined(CAIRN_PUBLIC_RELEASE)
namespace
{
bool loaded = false, ready = false;
}
#endif
bool confirmed()
{
#if defined(CAIRN_PUBLIC_RELEASE)
    if (loaded)
        return ready;
    loaded = true;
    Preferences prefs;
    if (!prefs.begin("cairn_setup", true))
        return false;
    ready = prefs.getBool("radio", false);
    prefs.end();
    return ready;
#else
    return true;
#endif
}
bool confirm()
{
#if defined(CAIRN_PUBLIC_RELEASE)
    Preferences prefs;
    if (!prefs.begin("cairn_setup", false))
        return false;
    const bool saved = prefs.putBool("radio", true) == 1;
    prefs.end();
    if (saved)
        ready = loaded = true;
    return saved;
#else
    return true;
#endif
}
} // namespace CairnRadioSetup
