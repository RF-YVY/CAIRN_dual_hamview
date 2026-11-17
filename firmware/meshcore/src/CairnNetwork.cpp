#include <lwip/tcpip.h>

extern "C" void __real_dns_clear_cache();

extern "C" void __wrap_dns_clear_cache()
{
    // The pinned Arduino network manager clears DNS outside lwIP's core lock.
    const bool held = sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER);
    if (!held)
        LOCK_TCPIP_CORE();
    __real_dns_clear_cache();
    if (!held)
        UNLOCK_TCPIP_CORE();
}
