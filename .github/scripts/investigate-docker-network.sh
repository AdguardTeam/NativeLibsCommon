#!/bin/sh
# AG-55096 recon: can we wrap `docker network create` so the runner's
# container/service-job network gets IPv6? The runner calls, on Linux:
#   docker network create --label <DockerInstanceLabel> <network>
# with no --ipv6 and no way to inject it (actions/runner#3599). A PATH shim on
# the `docker` binary is the candidate fix. This gathers whether that is viable:
# PATH precedence for a shim, --ipv6 support, the subnet a created network gets,
# and whether a container on it actually routes out. Read-only apart from a
# throwaway test network it creates and removes.
set +e

section() { printf '\n========== %s ==========\n' "$1"; }

section "docker binary resolution"
real=$(command -v docker)
echo "real docker: ${real:-<none>}"
ls -l "$real" 2>/dev/null
command -v which >/dev/null 2>&1 && which -a docker 2>/dev/null

section "PATH precedence (a shim must sit in a writable dir before the real docker)"
# The runner resolves `docker` via PATH (WhichUtil), so a shim earlier in PATH
# wins — provided that dir precedes the real binary in the runner's own env.
i=0
IFS=:
for d in $PATH; do
  i=$((i + 1))
  mark=""
  [ -x "$d/docker" ] && mark=" <-- has docker"
  printf '%2d  %-22s writable=%s%s\n' "$i" "$d" "$([ -w "$d" ] && echo yes || echo no)" "$mark"
done
unset IFS

section "Runner process PATH (what it actually searches when calling docker)"
# Must match, or precede the real docker in, this env — not just the step's.
for p in $(pgrep -f 'Runner\.(Listener|Worker)|/opt/actions-runner/run' 2>/dev/null); do
  cmd=$(tr '\0' ' ' </proc/"$p"/cmdline 2>/dev/null | cut -c1-50)
  path=$(tr '\0' '\n' </proc/"$p"/environ 2>/dev/null | sed -n 's/^PATH=//p')
  echo "pid $p ($cmd)"
  echo "  PATH=$path"
done

section "docker network create --ipv6 support + daemon config"
docker --version
docker network create --help 2>&1 | grep -iE 'ipv6|subnet' || echo "(no --ipv6 flag?)"
echo "--- daemon.json ---"; cat /etc/docker/daemon.json 2>/dev/null || echo "(none)"
echo "--- default-address-pools (needed to auto-assign a v6 subnet) ---"
docker info --format '{{json .DefaultAddressPools}}' 2>/dev/null

section "Functional test: the runner's create, plain vs. wrapped with --ipv6"
echo "# plain (exactly what the runner issues today):"
docker network create --label ag55096 ag55096-plain >/dev/null 2>&1 &&
  docker network inspect ag55096-plain \
    --format 'EnableIPv6={{.EnableIPv6}} subnets=[{{range .IPAM.Config}}{{.Subnet}} {{end}}]'
echo "# wrapped (--ipv6 appended, as a shim would):"
docker network create --ipv6 --label ag55096 ag55096-v6 >/dev/null 2>&1 &&
  docker network inspect ag55096-v6 \
    --format 'EnableIPv6={{.EnableIPv6}} subnets=[{{range .IPAM.Config}}{{.Subnet}} {{end}}]'

section "NAT/forward rules after --ipv6 network create (why doesn't it route?)"
# docker run (default bridge) routes via our fc00::/7 SNAT, but runner-created
# --ipv6 networks do not — most likely Docker adds its own masquerade for user
# networks that picks a non-routable source. Show who touches these packets.
echo "--- ip6tables -t nat -S ---"; ip6tables -t nat -S 2>&1
echo "--- ip6tables -S FORWARD (head) ---"; ip6tables -S FORWARD 2>&1 | head -20
echo "--- nft ip6 ruleset (Docker 28+ native firewall backend) ---"
nft list ruleset ip6 2>/dev/null | grep -iE 'table|chain|masquerade|snat|drop|fd20|fdc7|fc00' | head -50 ||
  echo "(nft unavailable)"
echo "--- docker firewall backend ---"; docker info 2>/dev/null | grep -iE 'firewall|iptables'

section "Container on the wrapped network: address + outbound IPv6"
# Note: the baked SNAT only masquerades the default-bridge prefix, so a new
# network's subnet may get an address but still fail to route — that tells us
# the wrapper also needs a known subnet the SNAT covers (or a broader rule).
docker run --rm --network ag55096-v6 "$CORELIBS_IMAGE" sh -c '
  ip -6 addr show scope global 2>/dev/null | sed -n "s/.*inet6 /  inet6 /p" || echo "  (no global v6)"
  curl -6 -sS --max-time 15 -o /dev/null -w "  curl -6 -> HTTP %{http_code}\n" \
    https://ipv6.google.com || echo "  curl -6 FAILED"'

docker network rm ag55096-plain ag55096-v6 >/dev/null 2>&1
echo
echo "docker-network recon finished (report-only)."
