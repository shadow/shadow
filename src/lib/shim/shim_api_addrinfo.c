/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */
#include "lib/shim/shim_api_c.h"

#include <arpa/inet.h>
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_syscall.h"

// Sets `port` to the port specified by `service`, according to the criteria in
// getaddrinfo(3). Returns 0 on success or the appropriate getaddrinfo error on
// failure.
static int _getaddrinfo_service(in_port_t* port, const char* service,
                                const struct addrinfo* hints) {
    char* endptr;
    *port = htons(strtol(service, &endptr, 10));
    if (*service != '\0' && *endptr == '\0') {
        return 0;
    }

    // getaddrinfo(3): "EAI_NONAME: ... or AI_NUMERICSERV was specified in
    // hints.ai_flags and service was not a numeric port-number string."
    if (hints->ai_flags & AI_NUMERICSERV) {
        return EAI_NONAME;
    }

    // `buf` will be used for strings pointed to in `result`.
    // 1024 is the recommended size in getservbyname_r(3).
    char buf[1024];
    struct servent servent;
    struct servent* result;
    int rv = getservbyname_r(service, NULL, &servent, buf, 1024, &result);
    if (rv != 0) {
        // According to getservbyname_r(3): "On error, they return one of the
        // positive error numbers listed in errors." The only one documented as
        // possibly being returned by getserbyname_r is ERANGE, indicating that
        // the buffer was too small. We *could* retry with a bigger buffer, but
        // that really shouldn't be needed.
        //
        // getaddrinfo(3): "EAI_SYSTEM: Other system error, check errno for
        // details."
        if (rv == EBADF || rv == ENOENT) {
            // In cases where libc wasn't able to connect to a local resolver
            // (which is expected under Shadow), and the service wasn't found in
            // /etc/services, some versions of libc return non-zero rv and
            // errno=EBADF or ENOENT.
            // https://github.com/shadow/shadow/issues/1869
            // https://github.com/shadow/shadow/issues/2286
            warning("Converting err %d to EAI_SERVICE to work around #1869 or #2286", rv);
            return EAI_SERVICE;
        }
        errno = rv;
        return EAI_SYSTEM;
    }
    if (result == NULL) {
        // getaddrinfo(3): "The  requested  service  is not available for the
        // requested socket type."
        return EAI_SERVICE;
    }
    // While getaddrinfo(3) seems to indicate that we should restrict which
    // protocols we return based on the specific service, and fail if the
    // service we found was incompatible with the requested socket type or
    // protocol, experimentally glibc doesn't do this. e.g., for "80" or "http"
    // it will return UDP and RAW in addition to TCP, despite /etc/services
    // only containing a TCP entry for that protocol.
    *port = result->s_port;
    return rv;
}

// Creates an `addrinfo` pointing to `addr`, and adds it to the linked list
// specified by `head` and `tail`. An empty list can be passed in by setting
// `*head` and `*tail` to NULL.
static void _getaddrinfo_append(struct addrinfo** head, struct addrinfo** tail, int socktype,
                                struct sockaddr* addr, socklen_t addrlen) {
    int protocol = 0;
    if (socktype == SOCK_DGRAM) {
        protocol = IPPROTO_UDP;
    }
    if (socktype == SOCK_STREAM) {
        protocol = IPPROTO_TCP;
    }
    if (socktype == SOCK_RAW) {
        protocol = 0;
    }
    struct addrinfo* new_tail = malloc(sizeof(*new_tail));
    *new_tail = (struct addrinfo){.ai_flags = 0,
                                  .ai_family = AF_INET,
                                  .ai_socktype = socktype,
                                  .ai_protocol = protocol,
                                  .ai_addrlen = addrlen,
                                  .ai_addr = addr,
                                  .ai_canonname = NULL,
                                  .ai_next = NULL};
    if (*tail != NULL) {
        (*tail)->ai_next = new_tail;
    }
    *tail = new_tail;
    if (*head == NULL) {
        *head = new_tail;
    }
}

// IPv4 wrapper for _getaddrinfo_append. Appends an entry for the address and
// port for each requested socket type.
static void _getaddrinfo_appendv4(struct addrinfo** head, struct addrinfo** tail, bool add_tcp,
                                  bool add_udp, bool add_raw, uint32_t s_addr, in_port_t port) {
    if (add_tcp) {
        struct sockaddr_in* sai = malloc(sizeof(*sai));
        *sai = (struct sockaddr_in){.sin_family = AF_INET, .sin_port = port, .sin_addr = {s_addr}};
        _getaddrinfo_append(head, tail, SOCK_STREAM, (struct sockaddr*)sai, sizeof(*sai));
    }
    if (add_udp) {
        struct sockaddr_in* sai = malloc(sizeof(*sai));
        *sai = (struct sockaddr_in){.sin_family = AF_INET, .sin_port = port, .sin_addr = {s_addr}};
        _getaddrinfo_append(head, tail, SOCK_DGRAM, (struct sockaddr*)sai, sizeof(*sai));
    }
    if (add_raw) {
        struct sockaddr_in* sai = malloc(sizeof(*sai));
        *sai = (struct sockaddr_in){.sin_family = AF_INET, .sin_port = port, .sin_addr = {s_addr}};
        _getaddrinfo_append(head, tail, SOCK_RAW, (struct sockaddr*)sai, sizeof(*sai));
    }
}

// Looks for matching IPv4 addresses in /etc/hosts and them to the list
// specified by `head` and `tail`.
static void _getaddrinfo_add_matching_hosts_ipv4(struct addrinfo** head, struct addrinfo** tail,
                                                 const char* node, bool add_tcp, bool add_udp,
                                                 bool add_raw, in_port_t port) {
    // TODO: Parse hosts file once and keep it in an efficiently-searchable
    // in-memory format.
    GError* error = NULL;
    gchar* hosts = NULL;
    char* pattern = NULL;
    GMatchInfo* match_info = NULL;
    GRegex* regex = NULL;

    trace("Reading /etc/hosts file");

    g_file_get_contents("/etc/hosts", &hosts, NULL, &error);
    if (error != NULL) {
        panic("Reading /etc/hosts: %s", error->message);
        goto out;
    }
    assert(hosts != NULL);

    trace("Scanning /etc/hosts contents for name %s", node);

    {
        gchar* escaped_node = g_regex_escape_string(node, -1);
        // Build a regex to match an IPv4 address entry for the given `node` in
        // /etc/hosts. See HOSTS(5) for format specification.
        int rv = asprintf(&pattern, "^(\\d+\\.\\d+\\.\\d+\\.\\d+)[^#\n]*\\b%s\\b", escaped_node);
        g_free(escaped_node);
        if (rv < 0) {
            panic("asprintf failed: %d", rv);
            goto out;
        }
    }
    trace("Node:%s -> regex:%s", node, pattern);

    regex = g_regex_new(pattern, G_REGEX_MULTILINE, 0, &error);
    if (error != NULL) {
        panic("g_regex_new: %s", error->message);
        goto out;
    }
    assert(regex != NULL);

    g_regex_match(regex, hosts, 0, &match_info);
    // /etc/host.conf specifies whether to return all matching addresses or only
    // the first. The recommended configuration is to only return the first. For
    // now we hard-code that behavior.
    if (g_match_info_matches(match_info)) {
#ifdef DEBUG
        {
            gchar* matched_string = g_match_info_fetch(match_info, 0);
            trace("Node:%s -> match:%s", node, matched_string);
            g_free(matched_string);
        }
#endif
        gchar* address_string = g_match_info_fetch(match_info, 1);
        trace("Node:%s -> address string:%s", node, address_string);
        assert(address_string != NULL);
        uint32_t addr;
        int rv = inet_pton(AF_INET, address_string, &addr);
        if (rv != 1) {
            panic("Bad address in /etc/hosts: %s\n", address_string);
        } else {
            _getaddrinfo_appendv4(head, tail, add_tcp, add_udp, add_raw, addr, port);
        }
        g_free(address_string);
    }
out:
    if (match_info != NULL)
        g_match_info_free(match_info);
    if (regex != NULL)
        g_regex_unref(regex);
    if (pattern != NULL)
        free(pattern);
    if (hosts != NULL)
        g_free(hosts);
}

// Ask shadow to provide an ipv4 addr for a node using a custom syscall.
// Returns true if we got a valid address from shadow, false otherwise.
static bool _shim_api_hostname_to_addr_ipv4(const char* node, uint32_t* addr) {
    if (!node || !addr) {
        return false;
    }

    // Skip the Shadow syscall for localhost lookups.
    if (strcasecmp(node, "localhost") == 0) {
        // Loopback address in network order.
        *addr = htonl(INADDR_LOOPBACK);
        trace("handled localhost getaddrinfo() lookup locally");
        return true;
    }

    // Resolve the hostname (find the ipv4 `addr` associated with hostname `name`) using a custom
    // syscall that Shadow handles internally. We want to execute natively in ptrace mode so ptrace
    // can intercept it, but we want to send to Shadow through shmem in preload mode. Let
    // shim_syscall figure it out.
    trace("Performing custom shadow syscall SYS_shadow_hostname_to_addr_ipv4 for name %s", node);
    int rv = shim_api_syscall(
        SHADOW_SYSCALL_NUM_HOSTNAME_TO_ADDR_IPV4, node, strlen(node), addr, sizeof(*addr));

    if (rv == 0) {
#ifdef DEBUG
        char addr_str_buf[INET_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET, (struct in_addr*)addr, addr_str_buf, INET_ADDRSTRLEN)) {
            trace("SYS_shadow_hostname_to_addr_ipv4 returned addr %s for name %s", addr_str_buf,
                  node);
        } else {
            trace("SYS_shadow_hostname_to_addr_ipv4 succeeded for name %s", node);
        }
#endif
        return true;
    } else {
        trace("SYS_shadow_hostname_to_addr_ipv4 failed for name %s", node);
        return false;
    }
}

int shimc_api_getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                         struct addrinfo** res) {
    // Quoted text is from the man page.

    // "Either node or service, but not both, may be NULL."
    // "EAI_NONAME...both node and service are NULL"
    if (node == NULL && service == NULL) {
        return EAI_NONAME;
    }

    // "Specifying  hints  as  NULL  is  equivalent  to setting ai_socktype and
    // ai_protocol to 0; ai_family to AF_UNSPEC; and ai_flags to (AI_V4MAPPED |
    // AI_ADDRCONFIG).
    static const struct addrinfo default_hints = {.ai_socktype = 0,
                                                  .ai_protocol = 0,
                                                  .ai_family = AF_UNSPEC,
                                                  .ai_flags = AI_V4MAPPED | AI_ADDRCONFIG};
    if (hints == NULL) {
        hints = &default_hints;
    }

    // "`service` sets the port in each returned address structure."
    in_port_t port = 0;
    if (service != NULL) {
        int rv = _getaddrinfo_service(&port, service, hints);
        if (rv != 0) {
            return rv;
        }
    }

    // "There are several reasons why the linked list may have more than one
    // addrinfo structure, including: the network host is ... the same service
    // is available from multiple socket types (one SOCK_STREAM address and
    // another SOCK_DGRAM address, for example)."
    //
    // Experimentally, glibc doesn't pay attention to which protocols are
    // specified for the given port in /etc/services; it returns all protocols
    // that are compatible with `hints`. We do the same for compatibility.
    bool add_tcp = (hints->ai_socktype == 0 || hints->ai_socktype == SOCK_STREAM) &&
                   (hints->ai_protocol == 0 || hints->ai_protocol == IPPROTO_TCP);
    bool add_udp = (hints->ai_socktype == 0 || hints->ai_socktype == SOCK_DGRAM) &&
                   (hints->ai_protocol == 0 || hints->ai_protocol == IPPROTO_UDP);
    bool add_raw =
        (hints->ai_socktype == 0 || hints->ai_socktype == SOCK_RAW) && (hints->ai_protocol == 0);

    // "If hints.ai_flags includes the AI_ADDRCONFIG flag, then IPv4 addresses
    // are returned in the list pointed to by  res  only  if  the local  system
    // has at least one IPv4 address configured, and IPv6 addresses are
    // returned only if the local system has at least one IPv6 address
    // configured."
    //
    // Determining what kind of addresses the local system has configured is
    // unimplemented. For now we assume it has IPv4 and not IPv6.
    const bool system_has_an_ipv4_address = true;
    const bool system_has_an_ipv6_address = false;

    // "There are several reasons why the linked list may have more than one
    // addrinfo structure, including: the network host is ... accessible  over
    // multiple  protocols  (e.g., both AF_INET and AF_INET6)"
    //
    // Here we constrain which protocols to consider, so that we can not bother
    // doing lookups for other protocols.
    const bool add_ipv4 = hints->ai_family == AF_UNSPEC ||
                          (hints->ai_family == AF_INET &&
                           !((hints->ai_flags & AI_ADDRCONFIG) && !system_has_an_ipv4_address));
    const bool add_ipv6 = hints->ai_family == AF_UNSPEC ||
                          (hints->ai_family == AF_INET6 &&
                           !((hints->ai_flags & AI_ADDRCONFIG) && !system_has_an_ipv6_address));

    // "EAI_ADDRFAMILY: The specified network host does not have any network
    // addresses in the requested address family."
    if (!add_ipv4 && !add_ipv6) {
        return EAI_ADDRFAMILY;
    }

    // *res will be the head of the linked lists of results. For efficiency we
    // also keep track of the tail of the list.
    *res = NULL;
    struct addrinfo* tail = NULL;

    // No address lookups needed if `node` is NULL.
    if (node == NULL) {
        if (hints->ai_flags & AI_PASSIVE) {
            // "If the AI_PASSIVE flag is specified in hints.ai_flags, and node
            // is NULL, then the returned socket addresses will be suitable  for
            // bind(2)ing a socket that will accept(2) connections.  The
            // returned socket address will contain the "wildcard address"
            // (INADDR_ANY for IPv4 addresses, IN6ADDR_ANY_INIT for IPv6
            // address)."
            if (add_ipv4) {
                _getaddrinfo_appendv4(
                    res, &tail, add_tcp, add_udp, add_raw, ntohl(INADDR_ANY), port);
            }
            if (add_ipv6) {
                // TODO: IPv6
            }
        } else {
            // "If the AI_PASSIVE flag is not set in hints.ai_flags, then the
            // returned socket addresses will be suitable for use with
            // connect(2), sendto(2), or sendmsg(2). If node is NULL, then the
            // network address will be set to the loopback interface address
            // (INADDR_LOOP‐ BACK  for  IPv4  addresses, IN6ADDR_LOOPBACK_INIT
            // for IPv6 address);"
            if (add_ipv4) {
                _getaddrinfo_appendv4(
                    res, &tail, add_tcp, add_udp, add_raw, ntohl(INADDR_LOOPBACK), port);
            }
            if (add_ipv6) {
                // TODO: IPv6
            }
        }
        // We've finished adding all relevant addresses.
        if (*res != NULL) {
            return 0;
        } else {
            return EAI_NONAME;
        }
    }

    // "`node` specifies either a numerical network address..."
    if (add_ipv6) {
        // TODO: try parsing as IPv6
    }
    if (add_ipv4) {
        uint32_t addr;
        if (inet_pton(AF_INET, node, &addr) == 1) {
            _getaddrinfo_appendv4(res, &tail, add_tcp, add_udp, add_raw, addr, port);
        }
    }
    // If we successfully parsed as a numeric address, there's no need to
    // continue on to doing name-based lookups.
    if (*res != NULL) {
        return 0;
    }
    // "If  hints.ai_flags  contains the  AI_NUMERICHOST  flag,  then  node
    // must be a numerical network address."
    if (hints->ai_flags & AI_NUMERICHOST) {
        // "The node or service is not known; or both node and service are NULL;
        // or AI_NUMERICSERV was specified in hints.ai_flags and service was not
        // a numeric port-number string."
        //
        // The man page isn't 100% explicit about which error to return in this
        // case, but EAI_NONAME is plausible based on the above, and it's what
        // glibc returns.
        return EAI_NONAME;
    }

    // "node specifies either a  numerical network  address...or a network
    // hostname, whose network addresses are looked up and resolved."
    //
    // On to name lookups. The `hosts` line in /etc/nsswitch.conf specifies the
    // order in which to try lookups.  We just hard-code trying `files` first
    // (and for now, only). For hosts lookups, the corresponding file is
    // /etc/hosts. See NSSWITCH.CONF(5).
    if (add_ipv6) {
        // TODO: look for IPv6 addresses in /etc/hosts.
    }
    if (add_ipv4) {
        // Try first to avoid scanning the /etc/hosts file.
        uint32_t addr;
        if (_shim_api_hostname_to_addr_ipv4(node, &addr)) {
            // We got the address we needed.
            _getaddrinfo_appendv4(res, &tail, add_tcp, add_udp, add_raw, addr, port);
        } else {
            // Fall back to scanning /etc/hosts.
            warning("shadow_hostname_to_addr_ipv4 syscall failed for name %s, falling back to less "
                    "efficient scan of '/etc/hosts' file.",
                    node);
            _getaddrinfo_add_matching_hosts_ipv4(res, &tail, node, add_tcp, add_udp, add_raw, port);
        }
    }

    // TODO: maybe do DNS lookup, if we end up supporting that in Shadow.

    if (*res == NULL) {
        // "EAI_NONAME: The node or service is not known"
        return EAI_NONAME;
    }
    return 0;
}

void shimc_api_freeaddrinfo(struct addrinfo* res) {
    while (res != NULL) {
        struct addrinfo* next = res->ai_next;
        assert(res->ai_addr != NULL);
        free(res->ai_addr);
        // We don't support canonname lookups, so shouldn't have been set.
        assert(res->ai_canonname == NULL);
        free(res);
        res = next;
    }
}
