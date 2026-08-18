#!/bin/sh
# AG-55096 recon: find where the VM agent launches actions-runner, so a boot
# hook (enable-docker-ipv6.sh) can be injected before the runner starts when the
# runner image is rebuilt. Read-only — it inspects, never changes anything, and
# never reads runner secrets (.credentials, .runner).
set +e

section() { printf '\n========== %s ==========\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }
show() { echo "\$ $*"; "$@" 2>&1; echo; }

section "Identity and OS"
show id
show uname -a
[ -f /etc/os-release ] && cat /etc/os-release

section "Runner / agent processes (who is PID-tree parent of the job?)"
show sh -c "ps -eo pid,ppid,user,lstart,args | grep -iE 'runner|Runner.Listener|run(svc)?\.sh|vm-?agent|qocker' | grep -v grep"

section "Runner / agent systemd units"
show sh -c "systemctl list-units --all --type=service --no-pager | grep -iE 'runner|agent|ephemeral|qocker'"
# Full definition of each matching unit — ExecStartPre here is the natural
# injection point for a pre-start hook.
for u in $(systemctl list-unit-files --no-pager 2>/dev/null \
    | awk '/runner|agent|ephemeral|qocker/ {print $1}'); do
  echo "----- systemctl cat $u -----"
  systemctl cat "$u" 2>&1
  echo "----- status -----"
  systemctl show "$u" -p FragmentPath -p ExecStartPre -p ExecStart -p WorkingDirectory \
    -p User -p After -p Wants -p Requires 2>&1
  echo
done

section "Runner install dir (non-secret files only)"
for d in /opt/actions-runner /actions-runner /home/*/actions-runner \
         /mnt/*/actions-runner /run/actions-runner; do
  [ -d "$d" ] || continue
  echo "----- $d -----"
  ls -la "$d" 2>&1
  for f in .env .path .setup run.sh runsvc.sh env.sh config.sh; do
    [ -f "$d/$f" ] && { echo "--- $d/$f ---"; cat "$d/$f" 2>&1; echo; }
  done
done

section "Job hooks (runner-level pre/post-job scripts)"
show sh -c "env | grep -iE 'ACTIONS_RUNNER_HOOK|RUNNER_' | sort"
show sh -c "mount | grep -iE 'shared|/mnt'"
for d in /mnt/shared /mnt/shared/work; do
  [ -d "$d" ] && { echo "----- $d -----"; ls -la "$d" 2>&1; echo; }
done
for f in /mnt/shared/work/job-started.sh /mnt/shared/work/job-completed.sh; do
  [ -f "$f" ] && { echo "--- $f ---"; cat "$f" 2>&1; echo; }
done

section "Boot-time provisioning (cloud-init / rc.local / first-boot units)"
have cloud-init && show cloud-init status --long
for p in /var/lib/cloud/instance/scripts /var/lib/cloud/instance/user-data.txt \
         /etc/cloud/cloud.cfg.d /etc/rc.local; do
  [ -e "$p" ] && { echo "----- $p -----"; ls -la "$p" 2>&1; \
    [ -f "$p" ] && cat "$p" 2>&1; echo; }
done
show sh -c "systemctl list-units --all --type=service --no-pager | grep -iE 'cloud-init|firstboot|first-boot|provision'"

section "Docker: current state and what a hook would change"
show sh -c "systemctl is-active docker; systemctl is-enabled docker"
[ -f /etc/docker/daemon.json ] && { echo "--- /etc/docker/daemon.json ---"; \
  cat /etc/docker/daemon.json 2>&1; echo; } || echo "no /etc/docker/daemon.json yet"
show systemctl show docker -p FragmentPath -p ExecStart -p After -p Wants

section "Candidate injection points (summary)"
cat <<'NOTES'
Look, in priority order, for a place that runs as root at boot BEFORE the
runner registers:
  1. An ExecStartPre= drop-in on the runner's systemd unit (FragmentPath above)
     -> /etc/systemd/system/<unit>.d/10-ipv6.conf with the script path.
  2. A dedicated oneshot unit ordered Before= the runner unit and After=docker.
  3. cloud-init runcmd / a first-boot script, if the image is cloud-init driven.
  4. As a fallback, the job-started hook (runs per job, after runner start) —
     works but re-applies every job instead of once at boot.
Whichever the runner image rebuild can bake in wins; the recon above shows which
exist on this agent.
NOTES
