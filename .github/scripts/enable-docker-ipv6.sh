#!/bin/sh
# Give Docker containers routable IPv6 on the ephemeral Linux agent (AG-55096).
set -eu

PREFIX="fd00:dead:beef::/64"

mkdir -p /etc/docker
cat > /etc/docker/daemon.json <<JSON
{
  "ipv6": true,
  "fixed-cidr-v6": "${PREFIX}"
}
JSON
systemctl restart docker

sysctl -w net.ipv6.conf.all.forwarding=1
sysctl -w net.ipv6.conf.all.accept_ra=2
sysctl -w net.ipv6.conf.default.accept_ra=2

# SNAT the container prefix to the uplink's own address; left to itself the
# kernel picks the non-routable docker0 ULA (global scope) as the source.
IF=$(ip -6 route get 2001:4860:4860::8888 | sed -n 's/.* dev \([^ ]*\).*/\1/p')
SRC=$(ip -6 addr show dev "$IF" |
  awk '/inet6/ && $2 !~ /^fe80|^fd00:dead:beef/ { sub(/\/.*/, "", $2); print $2; exit }')
ip6tables -I FORWARD 1 -s "$PREFIX" -j ACCEPT
ip6tables -I FORWARD 1 -d "$PREFIX" -j ACCEPT
ip6tables -t nat -I POSTROUTING 1 -s "$PREFIX" -o "$IF" -j SNAT --to-source "$SRC"
