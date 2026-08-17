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
# ways (inserted at the top to beat the policy), then masquerade it out through
# the host uplink so containers reach IPv6 with a routable source address.
$SUDO ip6tables -I FORWARD 1 -s "${PREFIX}" -j ACCEPT
$SUDO ip6tables -I FORWARD 1 -d "${PREFIX}" -j ACCEPT
$SUDO ip6tables -t nat -A POSTROUTING -s "${PREFIX}" ! -o docker0 -j MASQUERADE

# Diagnostics: what the kernel would pick as the egress source, and the rules in
# force. Useful when a run shows containers with an address but no connectivity.
echo "--- ip -6 route get (egress source the kernel picks) ---"
$SUDO ip -6 route get 2001:4860:4860::8888 || true
echo "--- ip6tables nat POSTROUTING ---"
$SUDO ip6tables -t nat -S POSTROUTING
echo "--- ip6tables FORWARD (head) ---"
$SUDO ip6tables -S FORWARD | head
