#include "common/route_resolver.h"

#if defined(__APPLE__) && defined(__MACH__)

#include <TargetConditionals.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <mutex>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <vector>

#include "common/clock.h"
#include "common/defs.h"
#include "common/logger.h"
#include "common/net_utils.h"
#include "common/utils.h"

#if TARGET_OS_IPHONE
#define RTF_UP 0x1            /* route usable */
#define RTF_STATIC 0x800      /* manually added */
#define RTF_WASCLONED 0x20000 /* route generated through cloning */
#define RTF_IFSCOPE 0x1000000 /* has valid interface scope */

#define RTAX_DST 0     /* destination sockaddr present */
#define RTAX_NETMASK 2 /* netmask sockaddr present */
#define RTAX_MAX 8     /* size of array to allocate */

struct rt_metrics {
    u_int32_t rmx_locks;     /* Kernel leaves these values alone */
    u_int32_t rmx_mtu;       /* MTU for this path */
    u_int32_t rmx_hopcount;  /* max hops expected */
    int32_t rmx_expire;      /* lifetime for route, e.g. redirect */
    u_int32_t rmx_recvpipe;  /* inbound delay-bandwidth product */
    u_int32_t rmx_sendpipe;  /* outbound delay-bandwidth product */
    u_int32_t rmx_ssthresh;  /* outbound gateway buffer limit */
    u_int32_t rmx_rtt;       /* estimated round trip time */
    u_int32_t rmx_rttvar;    /* estimated rtt variance */
    u_int32_t rmx_pksent;    /* packets sent using this route */
    u_int32_t rmx_state;     /* route state */
    u_int32_t rmx_filler[3]; /* will be used for TCP's peer-MSS cache */
};

struct rt_msghdr2 {
    u_short rtm_msglen;        /* to skip over non-understood messages */
    u_char rtm_version;        /* future binary compatibility */
    u_char rtm_type;           /* message type */
    u_short rtm_index;         /* index for associated ifp */
    int rtm_flags;             /* flags, incl. kern & message, e.g. DONE */
    int rtm_addrs;             /* bitmask identifying sockaddrs in msg */
    int32_t rtm_refcnt;        /* reference count */
    int rtm_parentflags;       /* flags of the parent route */
    int rtm_reserved;          /* reserved field set to 0 */
    int rtm_use;               /* from rtentry */
    u_int32_t rtm_inits;       /* which metrics we are initializing */
    struct rt_metrics rtm_rmx; /* metrics themselves */
};
#else
#include <net/route.h>
#endif

// Routing socket alignment requirements
#define ROUNDUP(a) ((a) > 0 ? (1 + (((a) -1) | (sizeof(uint32_t) - 1))) : sizeof(uint32_t))

namespace ag {

static Error<RouteResolverError> dump_routing_table(Uint8Vector &rt, int family) {
    static constexpr int MAX_TRIES = 10;
    int n_try;
    for (n_try = 0; n_try < MAX_TRIES; ++n_try) {
        int name[] = {CTL_NET, PF_ROUTE, 0, family, NET_RT_DUMP2, 0};
        size_t out_size;
        if (sysctl(name, 6, nullptr, &out_size, nullptr, 0) < 0) {
            return make_error(RouteResolverError::AE_SYSCTL_ERROR, AG_FMT("estimate: {}, {}", errno, strerror(errno)));
        }
        assert(out_size < SIZE_MAX / 2);
        out_size *= 2;
        rt.resize(out_size);
        if (sysctl(name, 6, rt.data(), &out_size, nullptr, 0) < 0) {
            if (errno == ENOMEM) {
                continue;
            }
            return make_error(RouteResolverError::AE_SYSCTL_ERROR, AG_FMT("dump: {}, {}", errno, strerror(errno)));
        }
        rt.resize(out_size);
        return {};
    }
    return make_error(RouteResolverError::AE_NOMEM, AG_FMT("{} tries", n_try));
}

static std::string if_name(unsigned int if_index) {
    char ifnamebuf[IF_NAMESIZE];
    if (if_indextoname(if_index, ifnamebuf)) {
        return ifnamebuf;
    }
    return AG_FMT("(idx: {}, error: ({}) {})", if_index, errno, strerror(errno));
}

class AppleRouteResolver : public RouteResolver {
public:
    std::optional<uint32_t> resolve(const SocketAddress &address) override {
        std::scoped_lock l(cache_mutex);

        bool ipv4 = address.is_ipv4();
        bool cached = true;
        std::vector<IpRoute> *table_ptr = ipv4 ? &ipv4_cache : &ipv6_cache;
        std::vector<IpRoute> table_storage;

        if (auto now = SteadyClock::now(); now - created_at < std::chrono::seconds{NO_CACHE_SECONDS}) {
            // Don't read/write the cache during "no cache" period
            table_ptr = &table_storage;
        }
        if (table_ptr->empty()) {
            *table_ptr = read_routing_table(ipv4 ? AF_INET : AF_INET6);
            cached = false;
        }

        if (log.is_enabled(LogLevel::LOG_LEVEL_DEBUG)) {
            tracelog(log, "Using {} table ({} entries, {}):", ipv4 ? "IPv4" : "IPv6", table_ptr->size(),
                    cached ? "cached" : "just read");
            for (auto &route : *table_ptr) {
                auto addr = utils::addr_to_str({route.address.data(), (size_t) (ipv4 ? 4 : 16)});
                uint32_t prefix_len = 0;
                for (uint8_t b : route.netmask) {
                    prefix_len += __builtin_popcount(b);
                }
                tracelog(log, "{}/{} -> {}", addr, prefix_len, if_name(route.if_index));
            }
            tracelog(log, "End of table");
        }

        for (auto &route : *table_ptr) {
            if (route.matches(address.addr_unmapped())) {
                dbglog(log, "Match {} -> {}", address.str(), if_name(route.if_index));
                return route.if_index;
            }
        }

        dbglog(log, "No match for {}", address.str());
        return std::nullopt;
    }

    void flush_cache() override {
        dbglog(log, "Flushing routing cache");
        std::scoped_lock l(cache_mutex);
        ipv4_cache = {};
        ipv6_cache = {};
    }

private:
    // Amount of time since creation of resolver during which the cache is not used
    static constexpr int NO_CACHE_SECONDS = 10;

    struct IpRoute {
        std::array<uint8_t, 16> address{};
        std::array<uint8_t, 16> netmask{};
        uint32_t if_index{0};

        bool matches(Uint8View dest) const {
            for (size_t i = 0; i < dest.size() && i < netmask.size() && netmask[i] != 0; ++i) {
                if ((dest[i] & netmask[i]) != address[i]) {
                    return false;
                }
            }
            return true;
        }

        // More specific is less
        bool operator<(const IpRoute &o) const {
            // Masks are contiguous, lexicographically larger mask is more specific
            return netmask > o.netmask;
        }
    };

    Logger log{"RouteResolver"};

    std::mutex cache_mutex;
    std::vector<IpRoute> ipv4_cache;
    std::vector<IpRoute> ipv6_cache;
    SteadyClock::time_point created_at{SteadyClock::now()};

    std::vector<IpRoute> read_routing_table(int family) {
        std::vector<uint8_t> rt;
        auto error = dump_routing_table(rt, family);
        if (error) {
            warnlog(log, "Failed to dump routing table: {}", error->str());
            return {};
        }

        std::vector<IpRoute> table;
        size_t count = 0;
        for (uint8_t *next = rt.data(); next < rt.data() + rt.size(); ++count) {
            next += ((rt_msghdr2 *) next)->rtm_msglen;
        }
        table.reserve(count);
        for (uint8_t *next = rt.data(); next < rt.data() + rt.size();) {
            auto *rtm = (rt_msghdr2 *) next;
            next += rtm->rtm_msglen;

            // We are interested in usable, static routes that were not cloned
            // (BSD puts various stuff like ARP cache into the routing table)
            // We also skip routes with RTF_IFSCOPE flag set because we are
            // trying to reproduce the default routing behaviour,
            // (e.g. what the user would get if they ran `route get n.n.n.n`)
            // and Apple OSes will NOT use these routes by default:
            // https://superuser.com/questions/441075/what-traffic-uses-an-interface-bound-route-rtf-ifscope-flag
            if (!(rtm->rtm_flags & RTF_UP) || !(rtm->rtm_flags & RTF_STATIC) || (rtm->rtm_flags & RTF_WASCLONED)
                    || (rtm->rtm_flags & RTF_IFSCOPE)) {
                continue;
            }

            auto *sa_iter = (sockaddr *) (rtm + 1);
            if (sa_iter->sa_family != family) {
                continue;
            }

            sockaddr *rta[RTAX_MAX];
            for (int i = 0; i < RTAX_MAX; i++) {
                if (rtm->rtm_addrs & (1 << i)) {
                    rta[i] = sa_iter;
                    sa_iter = (sockaddr *) (ROUNDUP(sa_iter->sa_len) + (char *) sa_iter);
                } else {
                    rta[i] = nullptr;
                }
            }

            sockaddr *dst = rta[RTAX_DST];
            if (!dst) {
                continue;
            }

            IpRoute &route = table.emplace_back();
            route.if_index = rtm->rtm_index;

            // NOTE: netmask is in internal kernel format, not a usual sockaddr
            sockaddr *netmask = rta[RTAX_NETMASK];
            switch (dst->sa_family) {
            case AF_INET: {
                auto *addr = (uint8_t *) &((sockaddr_in *) dst)->sin_addr.s_addr;
                std::memcpy(&route.address[0], addr, 4);
                if (!netmask) { // Host route
                    std::memset(&route.netmask[0], 0xff, 4);
                    break;
                }
                if (netmask->sa_len <= offsetof(sockaddr_in, sin_addr)) { // Default route
                    break;
                }
                auto *mask = (uint8_t *) &((sockaddr_in *) netmask)->sin_addr.s_addr;
                std::memcpy(&route.netmask[0], mask, netmask->sa_len - offsetof(sockaddr_in, sin_addr));
                break;
            }
            case AF_INET6: {
                auto *addr = &((sockaddr_in6 *) dst)->sin6_addr;
                std::memcpy(&route.address[0], addr, 16);
                if (!netmask) { // Host route
                    std::memset(&route.netmask[0], 0xff, 16);
                    break;
                }
                if (netmask->sa_len <= offsetof(sockaddr_in6, sin6_addr)) { // Default route
                    break;
                }
                auto *mask = &((sockaddr_in6 *) netmask)->sin6_addr;
                std::memcpy(&route.netmask[0], mask, netmask->sa_len - offsetof(sockaddr_in6, sin6_addr));
                break;
            }
            default:
                assert(0);
                break;
            }
        }

        std::sort(table.begin(), table.end());

        return table;
    }
};

RouteResolverPtr RouteResolver::create() {
    return std::make_unique<AppleRouteResolver>();
}

} // namespace ag

#else // defined(__APPLE__) && defined(__MACH__)

namespace ag {

class NoopRouteResolver : public RouteResolver {
public:
    std::optional<uint32_t> resolve(const SocketAddress &) override {
        return std::nullopt;
    }

    void flush_cache() override {
        // do nothing
    }
};

RouteResolverPtr RouteResolver::create() {
    return std::make_unique<NoopRouteResolver>();
}

} // namespace ag

#endif // defined(__APPLE__) && defined(__MACH__)
