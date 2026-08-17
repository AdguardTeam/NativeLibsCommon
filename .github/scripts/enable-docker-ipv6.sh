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

# The agent has no global IPv6 — only the address the host itself uses to reach
# the internet (a site-local fec0::/… handed out by RA on this network). NAT the
# container prefix onto exactly that address, chosen the same way the kernel
# picks it for the host's own traffic, so replies come back. A plain MASQUERADE
# refuses to pick a non-global source here, hence the explicit SNAT.
PUBLIC6="2001:4860:4860::8888"
ROUTE=$($SUDO ip -6 route get "$PUBLIC6" 2>/dev/null || true)
EGRESS_IF=$(printf '%s\n' "$ROUTE" | sed -n 's/.* dev \([^ ]*\).*/\1/p')
SRC=$(printf '%s\n' "$ROUTE" | sed -n 's/.* src \([0-9a-f:]*\).*/\1/p')
echo "egress interface: ${EGRESS_IF:-<none>}, source address: ${SRC:-<none>}"
if [ -z "$SRC" ] || [ -z "$EGRESS_IF" ]; then
  echo "ERROR: host has no IPv6 route to the internet; cannot give containers IPv6" >&2
  exit 1
fi

# Insert at the top so these beat any rules Docker installed.
$SUDO ip6tables -t nat -I POSTROUTING 1 -s "${PREFIX}" -o "${EGRESS_IF}" \
  -j SNAT --to-source "${SRC}"
$SUDO ip6tables -I FORWARD 1 -s "${PREFIX}" -j ACCEPT
$SUDO ip6tables -I FORWARD 1 -d "${PREFIX}" -j ACCEPT

echo "--- ip6tables nat POSTROUTING ---"
$SUDO ip6tables -t nat -S POSTROUTING
echo "--- ip6tables FORWARD (head) ---"
$SUDO ip6tables -S FORWARD | head
