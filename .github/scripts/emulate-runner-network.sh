#!/bin/sh
# AG-55096: emulate how the GitHub runner sets up a container-job network, so we
# can test IPv6 routing on that exact path from an ordinary job (the real
# container job is subject to scheduler quirks and only reports). The runner does,
# on Linux:
#   docker network create --label <instance> <network>          # shim adds --ipv6
#   docker create --network <network> --entrypoint tail <img> -f /dev/null
#   docker start <container>
#   docker exec <container> <step…>
# The setup is report-only; the probe's own exit code decides pass/fail.
set -u

NET="github_network_emul_$$"
CT="gh_job_emul_$$"
LABEL="com.github.actions.emulate"
IMG="${CORELIBS_IMAGE:-adguard/core-libs:2.12}"

cleanup() {
  docker rm -f "$CT" >/dev/null 2>&1 || true
  docker network rm "$NET" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "=== docker network create (runner form; shim should append --ipv6) ==="
docker network create --label "$LABEL" "$NET"
docker network inspect "$NET" \
  --format 'EnableIPv6={{.EnableIPv6}} subnets=[{{range .IPAM.Config}}{{.Subnet}} {{end}}]'

echo
echo "=== who NATs this network (our SNAT vs. any Docker masquerade) ==="
echo "--- ip6tables -t nat -S POSTROUTING ---"
ip6tables -t nat -S POSTROUTING 2>&1
echo "--- nft ip6 masquerade/snat (Docker 28+ native backend) ---"
nft list ruleset ip6 2>/dev/null | grep -iE 'masquerade|snat|fc00|fd[0-9a-f]' | head -30 ||
  echo "(nft unavailable)"

echo
echo "=== start the job container on the network (runner form) ==="
docker create --name "$CT" --label "$LABEL" --network "$NET" \
  --entrypoint tail "$IMG" -f /dev/null >/dev/null
docker start "$CT" >/dev/null
docker cp .github/scripts/ipv6-probe.sh "$CT":/probe.sh

echo
echo "=== probe inside the emulated job container ==="
docker exec "$CT" sh /probe.sh
exit $?
