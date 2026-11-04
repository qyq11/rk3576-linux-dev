#!/usr/bin/env bash
base=/home/rkshare/programs/sn100_shell
printf 'SERIAL=%s\nBOOT=%s\n' "$(tr -d '\0\n' </proc/device-tree/serial-number)" "$(cat /proc/sys/kernel/random/boot_id)"
for file in "$base/runtime/data/config.ini" "$base/runtime/data/config_Debug.ini" "$base/runtime/data/theme_config.ini"; do
    printf 'CONFIG_FILE=%s\n' "$file"
    stat -c 'OWNER=%u:%g MODE=%a BYTES=%s' "$file"
    sha256sum "$file"
    awk '/^[[:space:]]*\[/ {section=$0; print section;next} /^[[:space:]]*[A-Za-z_][A-Za-z_0-9]*[[:space:]]*=/ {split($0,a,"="); key=a[1];gsub(/[[:space:]]/,"",key);if(key ~ /^(DriverModel|SampleHoleCount|EmergencyHoleCount|ReagentPanelChannelCount|ChannelCount|ReportTableType|devIP|devPort|IP|Port|ip|port|host|hostName|Host|Address|address|IsDebug|isDebug|containerIsTube|UserType|userType|autoGet|autoSend)$/) print $0;else print key"=<value omitted>"}' "$file"
done
id lckfb
runuser -u lckfb -- sh -c 'for c in sqlite3 flock sha256sum ldd fuser; do command -v "$c" || true; done; test -r /home/rkshare/programs/sn100_shell/runtime/data/config.ini'
for directory in /userdata /userdata/shared; do
    if [[ -e $directory || -L $directory ]]; then stat -c '%F %u:%g %a %n' "$directory"; else printf 'ABSENT=%s\n' "$directory"; fi
done
df -Pk "$base" /tmp
printf 'ORIGINAL_MARKER='; cat "$base/runtime/data/.sn100-original-work-v1"
printf 'RUNTIME_MARKER='; cat "$base/runtime/offline-runtime.tag"
printf 'CONFIGURATION_BASIS_COMPLETE=1\n'
