#!/usr/bin/env bash
set -Eeuo pipefail
test "$(tr -d '\0' </proc/device-tree/serial-number)" = 8314b1e8ffd0321f
test "$(id -un)" = lckfb
base=/home/rkshare/programs/aca_flow_test
binary=$base/ACA_V1.0OffLineD-500
test "$(sha256sum "$binary" | cut -d' ' -f1)" = bfb23e400584ee9f3b44eee2680fdd8eff85c51729963510f8612c71926ae33f
chmod 700 "$binary"
mkdir -p "$base/runtime/core" "$base/runs/probe_01"
printf 'BOARD=board_b\nSERIAL=8314b1e8ffd0321f\n'
sha256sum "$binary"
ldd "$binary" | tee "$base/runs/probe_01/dependencies.log"
! grep -q 'not found' "$base/runs/probe_01/dependencies.log"
test ! -e /userdata/shared
ss -lntp | tee "$base/runs/probe_01/listeners_before.log"
sudo -n systemd-run --unit=sn100-aca-probe-20261102-01 --service-type=exec \
  --uid=lckfb --property=WorkingDirectory="$base/runtime" \
  --property=PrivateDevices=yes --property=PrivateMounts=yes \
  --property=ProtectSystem=strict --property=ProtectHome=read-only \
  --property=ProtectKernelTunables=yes --property=ProtectKernelModules=yes \
  --property=NoNewPrivileges=yes --property=CapabilityBoundingSet= \
  --property='RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6' \
  --property="BindPaths=$base/runtime:/userdata/shared" \
  --property="ReadWritePaths=$base/runtime" --property=RuntimeMaxSec=30s \
  --property=LimitCORE=0 --property=TimeoutStopSec=5s \
  --property="StandardOutput=append:$base/runs/probe_01/program.log" \
  --property="StandardError=append:$base/runs/probe_01/program.log" \
  /usr/bin/stdbuf -oL -eL "$binary"
sleep 3
sudo -n systemctl show sn100-aca-probe-20261102-01 -p ActiveState -p SubState -p MainPID -p ExecMainStatus -p ExecMainCode -p Result
ss -lntp
tail -60 "$base/runs/probe_01/program.log"
