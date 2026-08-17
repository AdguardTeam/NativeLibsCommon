#!/bin/sh
# AG-55096: give Docker containers routable IPv6 on the ephemeral Linux agent,
# for the `docker run` and `docker build` paths. Run on the host before either.
set -eux

# Privileged commands run as root directly, otherwise through sudo.
if [ "$(id -u)" -ne 0 ]; then SUDO=sudo; else SUDO=; fi

# A ULA prefix out of fc00::/7 for the Docker bridge. Same value in
# daemon.json (fixed-cidr-v6) and the masquerade rule below.
PREFIX="fd00:dead:beef::/64"

$SUDO mkdir -p /etc/docker
# Throwaway agent, so overwrite rather than merge the existing file.
$SUDO tee /etc/docker/daemon.json >/dev/null <<JSON
{
  "ipv6": true,
  "fixed-cidr-v6": "${PREFIX}"
}
JSON

# Apply the new daemon config.
$SUDO systemctl restart docker || $SUDO service docker restart

# Container IPv6 is only routed off the host if the kernel forwards it. Enabling
# forwarding turns the host into a router, which by default stops it accepting
# Router Advertisements — so force accept_ra=2 to keep the SLAAC-learned default
# route alive.
$SUDO sysctl -w net.ipv6.conf.all.forwarding=1
$SUDO sysctl -w net.ipv6.conf.all.accept_ra=2
$SUDO sysctl -w net.ipv6.conf.default.accept_ra=2

# The default FORWARD policy is DROP once Docker is up, so allow the prefix both
# ways (inserted at the top to beat the policy).
$SUDO ip6tables -I FORWARD 1 -s "${PREFIX}" -j ACCEPT
$SUDO ip6tables -I FORWARD 1 -d "${PREFIX}" -j ACCEPT

# NAT the container prefix out through the host uplink. A plain MASQUERADE picks
# the source the same way the kernel does, and this agent has no global IPv6 —
# only the site-local address QEMU user-mode networking NATs upstream. Because a
# ULA counts as global scope, the kernel prefers the docker0 ULA over that
# site-local address and masquerades to a source that cannot receive replies. So
# pin the source explicitly to the uplink's own address instead.
PUBLIC6="2001:4860:4860::8888"
EGRESS_IF=$($SUDO ip -6 route get "$PUBLIC6" 2>/dev/null \
  | sed -n 's/.* dev \([^ ]*\).*/\1/p')
if [ -z "$EGRESS_IF" ]; then
  echo "ERROR: host has no IPv6 route to the internet" >&2
  exit 1
fi
# The uplink's own routable address — NOT the route's `src`, which is the ULA
# the kernel wrongly prefers. Skip link-local and our own container prefix.
SRC=$($SUDO ip -6 addr show dev "$EGRESS_IF" | awk '
  /inet6/ && $2 !~ /^fe80/ && $2 !~ /^fd00:dead:beef/ {
    sub(/\/.*/, "", $2); print $2; exit
  }')
if [ -z "$SRC" ]; then
  echo "ERROR: no routable IPv6 address on ${EGRESS_IF}" >&2
  exit 1
fi
echo "egress interface: ${EGRESS_IF}, SNAT source: ${SRC}"
# Insert at the top so this beats the MASQUERADE rule Docker installs itself.
$SUDO ip6tables -t nat -I POSTROUTING 1 -s "${PREFIX}" -o "${EGRESS_IF}" \
  -j SNAT --to-source "${SRC}"

# Diagnostics: the rules in force, handy when a container has an address but no
# connectivity.
echo "--- ip6tables nat POSTROUTING ---"
$SUDO ip6tables -t nat -S POSTROUTING
echo "--- ip6tables FORWARD (head) ---"
$SUDO ip6tables -S FORWARD | head
