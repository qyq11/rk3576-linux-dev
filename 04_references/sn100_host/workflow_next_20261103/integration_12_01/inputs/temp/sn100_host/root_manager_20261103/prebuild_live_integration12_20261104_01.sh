set -Eeuo pipefail
base=/home/rkshare/programs/sn100_shell
printf 'BOARD=board_b\nSERIAL='; tr -d '\0\n' </proc/device-tree/serial-number; printf '\nUTC='; date -u +%Y-%m-%dT%H:%M:%SZ
printf 'BOOT='; cat /proc/sys/kernel/random/boot_id
printf 'CURRENT='; readlink "$base/current"
[[ $(readlink "$base/current") == releases/c7cbf438a3a034b25ae22accfaf48bacf0717174ce85bef2203ce8db143cb06e ]]
manifest="$base/verification/integration_next_02_runtime_01/configuration.sha256"
printf '%s  %s\n' ad11ae68d8ed119a5a34ed65acf98dc2d15bafba6afaa950bee5fd7d3805981c "$manifest" | sha256sum -c -
sha256sum -c "$manifest"
for node in /proc/[0-9]*/exe; do
  value=$(readlink "$node" 2>/dev/null || true)
  case ${value##*/} in
    sn100_shell|ACA_V1.0OffLineD-500|rkaiq_3A_server)
      pid=${node#/proc/}; pid=${pid%/exe}
      printf 'PROCESS pid=%s exe=%s\n' "$pid" "$value"
      ps -p "$pid" -o pid=,stat=,lstart=,etimes=,rss=,comm=
      sha256sum "$node"
      ;;
  esac
done
systemctl show sn100-shell.service sn100-original-controller-board-b.service opto-camera.service -p Id -p ActiveState -p SubState -p MainPID -p MemoryCurrent -p Nice
ss -lntp '( sport = :5000 or sport = :8080 )'
ss -H -tnp '( sport = :5000 or dport = :5000 )'
printf 'READ_ONLY_PREBUILD_PROBE=1\nPROGRAM_SIGNALLED=0\nCURRENT_CHANGED=0\n'
