#!/bin/sh
# AG-55096 IPv6 probe. Prints what it finds on every path (host, container,
# docker run, docker build) and then exits non-zero when outbound IPv6 is not
# reachable, so a job's pass/fail reflects real connectivity. POSIX sh so it
# runs the same in a bash step and in a Dockerfile RUN.
set -u

rc=0

section() { printf '\n=== %s ===\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

section "Kernel IPv6 support"
if [ -f /proc/net/if_inet6 ]; then
  echo "PASS: /proc/net/if_inet6 present (kernel has IPv6)"
else
  echo "FAIL: /proc/net/if_inet6 missing (kernel IPv6 disabled)"
fi

section "IPv6 addresses (/proc/net/if_inet6)"
# Fields: address ifindex prefixlen scope flags ifname.
# scope 00 = global, 10 = host (loopback), 20 = link-local.
cat /proc/net/if_inet6 2>/dev/null || echo "(none)"
global6=$(awk '$4 == "00"' /proc/net/if_inet6 2>/dev/null | wc -l | tr -d ' ')
echo "Global-scope IPv6 addresses: ${global6}"

section "ip -6 addr"
if have ip; then ip -6 addr show || true; else echo "(ip not installed)"; fi

section "Outbound IPv6 connectivity"
# A missing route or firewall makes this the real signal, so run it even when a
# global address exists.
if have curl; then
  if code=$(curl -6 -sS --max-time 15 -o /dev/null \
            -w '%{http_code}' https://ipv6.google.com 2>&1); then
    echo "PASS: curl -6 https://ipv6.google.com -> HTTP ${code}"
  else
    echo "FAIL: curl -6 could not reach ipv6.google.com (${code})"
    rc=1
  fi
elif have ping6 || have ping; then
  p="ping6"; have ping6 || p="ping -6"
  if $p -c 2 -W 5 ipv6.google.com; then
    echo "PASS: ${p} reached ipv6.google.com"
  else
    echo "FAIL: ${p} could not reach ipv6.google.com"
    rc=1
  fi
else
  echo "FAIL: neither curl nor ping available to test connectivity"
  rc=1
fi

echo
if [ "$rc" -eq 0 ]; then
  echo "IPv6 probe: outbound IPv6 reachable."
else
  echo "IPv6 probe: outbound IPv6 NOT reachable."
fi
exit "$rc"
