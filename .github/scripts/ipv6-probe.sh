#!/bin/sh
# AG-55096 IPv6 probe. Report-only: it prints what it finds and never exits
# non-zero on a missing IPv6, so every path (host, container, docker run,
# docker build) reports side by side in one run. POSIX sh so it runs the same
# in a bash step and in a Dockerfile RUN.
set -u

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
  code=$(curl -6 -sS --max-time 15 -o /dev/null \
         -w '%{http_code}' https://ipv6.google.com 2>&1) \
    && echo "PASS: curl -6 https://ipv6.google.com -> HTTP ${code}" \
    || echo "FAIL: curl -6 could not reach ipv6.google.com (${code})"
elif have ping6 || have ping; then
  p="ping6"; have ping6 || p="ping -6"
  $p -c 2 -W 5 ipv6.google.com \
    && echo "PASS: ${p} reached ipv6.google.com" \
    || echo "FAIL: ${p} could not reach ipv6.google.com"
else
  echo "SKIP: neither curl nor ping available"
fi

echo
echo "IPv6 probe finished (report-only)."
