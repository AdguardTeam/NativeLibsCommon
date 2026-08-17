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

# Docker does not NAT IPv6, so masquerade the ULA prefix out through the host
# uplink; without this, containers get a source address that is not routable
# off the box.
$SUDO ip6tables -t nat -A POSTROUTING -s "${PREFIX}" -j MASQUERADE
