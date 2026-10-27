#!/usr/bin/env bash
# Target-side lifecycle for the unified offline shell. No device commands.
set -Eeuo pipefail
umask 077

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
valid_base() {
    [[ $1 =~ ^/[a-zA-Z0-9_./-]+$ && $1 != / ]] || die 'Invalid deployment root.'
    case $1/ in *'/../'*|*'/./'*|*'//'*) die 'Unnormalized deployment root.' ;; esac
}
has_reports() {
    [[ -f $1/release.json ]] && grep -q '"report_preview_pdf"' "$1/release.json"
}
has_samples() {
    [[ -f $1/release.json ]] && grep -q '"sample_local_presets"' "$1/release.json"
}
verify_release() {
    local release=$1 id line name
    local -A listed=([SHA256SUMS]=1 [release_id]=1)
    [[ -d $release && ! -L $release && -f $release/release_id && ! -L $release/release_id \
       && -f $release/SHA256SUMS && ! -L $release/SHA256SUMS ]] || die 'Missing regular release metadata.'
    id=$(cat "$release/release_id")
    [[ $id =~ ^[a-f0-9]{64}$ && ${release##*/} == "$id" ]] || die 'Release identity mismatch.'
    [[ $(sha256sum "$release/SHA256SUMS" | cut -d' ' -f1) == "$id" ]] || die 'Manifest identity mismatch.'
    while IFS= read -r line; do
        [[ $line =~ ^[a-f0-9]{64}\ \ ([a-zA-Z0-9_./-]+)$ ]] || die 'Unsafe manifest entry.'
        name=${BASH_REMATCH[1]}
        case /$name/ in *'/../'*|*'/./'*|*'//'*) die 'Unsafe manifest path.' ;; esac
        [[ $name != /* && ! ${listed[$name]+exists} ]] || die 'Duplicate or absolute manifest path.'
        listed[$name]=1
    done < "$release/SHA256SUMS"
    [[ -z $(find "$release" -type l -print -quit) ]] || die 'Symlinks are not allowed in an immutable release.'
    while IFS= read -r -d '' name; do
        name=${name#"$release/"}
        [[ ${listed[$name]+exists} ]] || die 'Release contains a file outside its manifest.'
    done < <(find "$release" -type f -print0)
    (cd "$release" && sha256sum --quiet -c SHA256SUMS) || die 'Release checksum failure.'
    [[ -x $release/sn100_shell && ! -L $release/sn100_shell ]] || die 'Missing regular executable.'
    if has_reports "$release"; then
        [[ -x $release/sn100_report_renderer && ! -L $release/sn100_report_renderer ]] || die 'Missing report renderer.'
        [[ -f $release/plugins/platforms/libqoffscreen.so ]] || die 'Missing report offscreen platform plugin.'
        for name in fonts/NotoSansCJK-Regular.ttc fonts/NotoSansCJK-Bold.ttc fonts/fonts.conf fonts/LICENSE.txt; do
            [[ -f $release/$name && ${listed[$name]+exists} ]] || die "Missing report font resource: $name"
        done
    fi
    if has_samples "$release"; then
        [[ -f $release/plugins/imageformats/libqsvg.so && ${listed[plugins/imageformats/libqsvg.so]+exists} ]] || die 'Missing sample SVG image plugin.'
    fi
}
current_release() {
    local base=$1 release
    release=$(readlink -f -- "$base/current") || return 1
    [[ $release == "$base/releases/"* && ${release#"$base/releases/"} != */* ]] || die 'Current release escapes deployment root.'
    verify_release "$release" >&2 || return 1
    printf '%s\n' "$release"
}
latest_run() {
    local base=$1 id
    id=$(cat "$base/latest_run") || return 1
    [[ $id =~ ^[0-9_]+$ && -d $base/runs/$id && ! -L $base/runs/$id ]] || die 'Invalid run identity.'
    printf '%s\n' "$base/runs/$id"
}
start_ticks() {
    local stat rest
    stat=$(cat "/proc/$1/stat") || return 1
    rest=${stat##*) }
    set -- $rest
    [[ $1 != Z && ${20:-} =~ ^[0-9]+$ ]] || return 1
    printf '%s\n' "${20}"
}
process_matches() {
    local run=$1 pid ticks expected
    [[ -f $run/app.pid && -f $run/app.start_ticks && -f $run/app.exe && -f $run/boot_id ]] || return 1
    pid=$(cat "$run/app.pid")
    [[ $pid =~ ^[1-9][0-9]*$ && -d /proc/$pid ]] || return 1
    [[ $(cat "$run/boot_id") == "$(cat /proc/sys/kernel/random/boot_id)" ]] || return 1
    ticks=$(start_ticks "$pid") || return 1
    [[ $ticks == "$(cat "$run/app.start_ticks")" ]] || return 1
    [[ $(stat -c %u "/proc/$pid") == "$EUID" ]] || return 1
    expected=$(cat "$run/app.exe")
    [[ $(readlink -- "/proc/$pid/exe") == "$expected" ]] || return 1
}
runtime_environment() {
    local release=$1
    unset DISPLAY WAYLAND_DISPLAY QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML2_IMPORT_PATH LD_PRELOAD FONTCONFIG_FILE FONTCONFIG_PATH QT_QPA_FONTDIR
    export LD_LIBRARY_PATH=$release/lib
    export QT_PLUGIN_PATH=$release/plugins:/usr/lib/aarch64-linux-gnu/qt5/plugins
    export QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib/aarch64-linux-gnu/qt5/plugins/platforms
    export QT_QPA_PLATFORM=eglfs QT_QPA_EGLFS_INTEGRATION=eglfs_kms
    if has_reports "$release"; then
        export FONTCONFIG_FILE=$release/fonts/fonts.conf QT_QPA_FONTDIR=$release/fonts
        export QT_QPA_PLATFORM_PLUGIN_PATH=$release/plugins/platforms:/usr/lib/aarch64-linux-gnu/qt5/plugins/platforms
        # Preflight must not create runtime/: supervise creates that directory
        # and its offline marker together when initializing a fresh deployment.
        export XDG_CACHE_HOME=${2:-${release%/releases/*}/preflight_cache}
        mkdir -p -- "$XDG_CACHE_HOME"
    fi
    # Account pages contain passwords: do not enable raw keyboard/input tracing.
    export QT_LOGGING_RULES='qt.qpa.eglfs.kms=true;qt.qpa.egldeviceintegration=true'
}
preflight() {
    local base=$1 release=$2 plugin output readable=0 event
    [[ $(uname -m) == aarch64 && $EUID != 0 ]] || die 'Use the ordinary RK aarch64 user.'
    verify_release "$release" || return 1
    runtime_environment "$release"
    local plugins=("$release/plugins/sqldrivers/libqsqlite.so"
        /usr/lib/aarch64-linux-gnu/qt5/plugins/platforms/libqeglfs.so
        /usr/lib/aarch64-linux-gnu/qt5/plugins/egldeviceintegrations/libqeglfs-kms-integration.so)
    if has_reports "$release"; then plugins+=("$release/plugins/platforms/libqoffscreen.so"); fi
    if has_samples "$release"; then plugins+=("$release/plugins/imageformats/libqsvg.so"); fi
    for plugin in "${plugins[@]}"; do
        [[ -f $plugin ]] || die "Missing target plugin: $plugin"
        output=$(LC_ALL=C ldd "$plugin") || return 1
        printf '[plugin] %s\n%s\n' "$plugin" "$output"
        ! grep -q 'not found' <<< "$output" || die 'Missing plugin dependency.'
    done
    output=$(LC_ALL=C ldd "$release/sn100_shell") || return 1
    printf '[application dependencies]\n%s\n' "$output"
    ! grep -q 'not found' <<< "$output" || die 'Missing application dependency.'
    if has_reports "$release"; then
        output=$(LC_ALL=C ldd "$release/sn100_report_renderer") || return 1
        printf '[report renderer dependencies]\n%s\n' "$output"
        ! grep -q 'not found' <<< "$output" || die 'Missing report renderer dependency.'
    fi
    [[ $(cat /sys/class/drm/card0-HDMI-A-1/status) == connected ]] || die 'HDMI is not connected.'
    [[ -r /dev/dri/card0 && -w /dev/dri/card0 ]] || die 'Current user cannot access DRM card0.'
    for event in /dev/input/event*; do if [[ -r $event ]]; then readable=1; fi; done
    [[ $readable == 1 ]] || die 'No readable input event device; arrange permissions on site.'
    command -v fc-match >/dev/null || die 'fontconfig tools are required for font verification.'
    output=$(fc-match -f '%{family}\n' 'Noto Sans CJK SC:lang=zh-cn')
    printf '[font] %s\n' "$output"
    [[ $output == *'Noto Sans CJK SC'* ]] || die 'Requested CJK font is unavailable.'
    [[ -w $base ]] || die 'Deployment directory is not writable.'
    printf 'PREFLIGHT_OK=1\n'
}
supervise() {
    local base=$1 release=$2 run=$3 seconds=$4 session=$5 data secret= child= child_start= code deadline close_sent=0 waited=0 supervisor_close=0 last_sample=-1
    valid_base "$base"
    [[ $(readlink -f "$base") == "$base" && ! -L $base/lifecycle.lock && ! -L $base/runs \
       && ! -L $base/runtime && ! -L $base/runtime/data && ! -L $run/runtime ]] || die 'Lifecycle path is a symlink or escapes its root.'
    [[ $run == "$base/runs/"* && ${run#"$base/runs/"} =~ ^[0-9_]+$ \
       && $(readlink -f "$run") == "$run" ]] || die 'Run must be a direct owned child.'
    [[ $(stat -c %u "$base") == "$EUID" && $(stat -c %u "$run") == "$EUID" ]] || die 'Lifecycle directories are not owned by this user.'
    [[ $seconds =~ ^(0|[1-9][0-9]{0,4})$ && $seconds -le 86400 ]] || die 'Invalid supervisor duration.'
    case $session in ''|offlineSend|offlineTest|offlineCheck|offlineEngineer|offlineAdmin) ;; *) die 'Invalid supervisor session.' ;; esac
    exec 9>"$base/lifecycle.lock"
    flock -w 10 9 || die 'Another lifecycle owns this deployment.'
    verify_release "$release" || return 1
    data=$base/runtime/data
    if [[ ! -d $base/runtime ]]; then
        mkdir -m 700 "$base/runtime"
        printf 'sn100-offline-runtime-v1\n' > "$base/runtime/offline-runtime.tag"
    fi
    [[ -f $base/runtime/offline-runtime.tag && ! -L $base/runtime/offline-runtime.tag \
       && $(cat "$base/runtime/offline-runtime.tag") == sn100-offline-runtime-v1 ]] || die 'Existing runtime root is not marked; refusing to overwrite it.'
    mkdir -p "$run/runtime"
    chmod 700 "$run" "$run/runtime"
    cleanup_secret() { if [[ -n ${secret:-} ]]; then rm -f -- "$secret"; secret=; fi; }
    child_is_owned() {
        [[ -n $child_start ]] || return 1
        [[ $(start_ticks "$child" 2>/dev/null) == "$child_start" \
           && $(readlink -- "/proc/$child/exe" 2>/dev/null) == "$release/sn100_shell" ]]
    }
    finalize_child() {
        [[ $waited == 0 ]] || return 0
        set +e
        wait "$child"
        code=$?
        set -e
        waited=1
        cleanup_secret
        local clean=0
        if [[ $code == 0 ]] && grep -qx 'HOSPITAL_STORE_CLOSED' "$run/app.log" \
            && grep -qx 'ACCOUNT_STORE_CLOSED' "$run/app.log" && grep -qx 'HISTORY_STORE_CLOSED' "$run/app.log" \
            && grep -q '^THREADS_STOPPED true$' "$run/app.log"; then clean=1; fi
        printf 'APP_EXIT=%s\nCLEAN_SHUTDOWN=%s\n' "$code" "$clean" > "$run/result.txt.new"
        mv -- "$run/result.txt.new" "$run/result.txt"
        # exit_code is the final commit marker: readers never see half a result.
        printf '%s\n' "$code" > "$run/exit_code.new"
        mv -- "$run/exit_code.new" "$run/exit_code"
    }
    supervisor_exit() {
        local failure=$?
        trap - EXIT
        set +e
        if [[ -n ${child:-} && ${waited:-0} == 0 ]]; then
            printf 'SUPERVISOR_FAILURE=%s\n' "$failure" >> "$run/runtime.txt"
            if child_is_owned; then kill -TERM "$child" 2>/dev/null || true; fi
            # A draft/busy guard still owns the application. Retain supervision
            # rather than abandoning it or escalating to SIGKILL.
            while child_is_owned; do sleep 0.2 || true; done
            finalize_child
        fi
        cleanup_secret
        exit "$failure"
    }
    trap supervisor_exit EXIT
    trap 'supervisor_close=1' TERM INT HUP
    local seed=() actor=()
    if [[ ! -f $data/account/user_Information.db ]]; then
        if [[ -d $data/account && -n $(find "$data/account" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
            die 'Partial account initialization exists; inspect it rather than reseeding.'; return 1
        fi
        secret=$(mktemp "$run/runtime/bootstrap.XXXXXXXX")
        cat /proc/sys/kernel/random/uuid > "$secret"
        seed=(--seed-anonymous --test-secret-file "$secret")
    fi
    [[ -z $session ]] || actor=(--test-session "$session")
    cat > "$run/kms.json" <<'KMS'
{"device":"/dev/dri/card0","hwcursor":false,"outputs":[{"name":"HDMI1","mode":"preferred","primary":true},{"name":"DP1","mode":"off"},{"name":"Writeback1","mode":"off"}]}
KMS
    runtime_environment "$release" "$run/runtime/cache"
    export XDG_RUNTIME_DIR=$run/runtime QT_QPA_EGLFS_KMS_CONFIG=$run/kms.json
    {
        uname -a
        id
        printf 'PREEMPT_RT='; cat /sys/kernel/realtime 2>/dev/null || printf 'unavailable\n'
        printf 'RELEASE_ID=%s\n' "${release##*/}"
        printf 'APP_SHA256='; sha256sum "$release/sn100_shell" | cut -d' ' -f1
        printf 'MODE=offline\nQPA=eglfs\nSOFT_DEADLINE_SECONDS=%s\n' "$seconds"
    } > "$run/runtime.txt"
    printf '%s\n' "${release##*/}" > "$run/release_id"
    cd -- "$release"
    printer_arguments=()
    if grep -q '"local_printer_settings"' "$release/release.json"; then printer_arguments=(--printer-settings); fi
    lis_arguments=()
    if grep -q '"local_lis_settings"' "$release/release.json"; then lis_arguments=(--lis-settings); fi
    quality_arguments=()
    if grep -q '"quality_control_local"' "$release/release.json"; then quality_arguments=(--quality-control); fi
    calibration_arguments=()
    if grep -q '"calibration_local"' "$release/release.json"; then calibration_arguments=(--calibration); fi
    alarm_history_arguments=()
    if grep -q '"alarm_history_readonly"' "$release/release.json"; then alarm_history_arguments=(--alarm-history); fi
    "$release/sn100_shell" --offline --accounts --history --catalog --samples --instrument --password-login "${printer_arguments[@]}" "${lis_arguments[@]}" "${quality_arguments[@]}" "${calibration_arguments[@]}" "${alarm_history_arguments[@]}" --data-dir "$data" --viewport 1600x900 \
        "${seed[@]}" "${actor[@]}" > "$run/app.log" 2>&1 &
    child=$!
    printf '%s\n' "$child" > "$run/app.pid"
    printf '%s\n' "$release/sn100_shell" > "$run/app.exe"
    cat /proc/sys/kernel/random/boot_id > "$run/boot_id"
    child_start=$(start_ticks "$child") || child_start=
    printf '%s\n' "$child_start" > "$run/app.start_ticks"
    printf 'uptime_s\tutime_ticks\tstime_ticks\trss_kib\tpss_kib\tthreads\tfd_count\ttemperatures_mC\tfrequencies_kHz\n' > "$run/resources.tsv"
    sample_resources() {
        local stat rest rss pss threads fds uptime temperatures= frequencies= node value
        [[ -r /proc/$child/status ]] || return 0
        stat=$(cat "/proc/$child/stat") || return 0
        rest=${stat##*) }; set -- $rest
        rss=$(awk '$1=="VmRSS:" {print $2}' "/proc/$child/status") || rss=unavailable
        threads=$(awk '$1=="Threads:" {print $2}' "/proc/$child/status") || threads=unavailable
        pss=$(awk '$1=="Pss:" {print $2}' "/proc/$child/smaps_rollup" 2>/dev/null) || pss=unavailable
        fds=$(find "/proc/$child/fd" -mindepth 1 -maxdepth 1 -type l 2>/dev/null | wc -l) || fds=unavailable
        read -r uptime _ < /proc/uptime
        for node in /sys/class/thermal/thermal_zone*/temp; do
            if [[ -r $node ]]; then value=$(cat "$node") || value=unavailable; temperatures+="${node%/temp}:$value;"; fi
        done
        for node in /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq; do
            if [[ -r $node ]]; then value=$(cat "$node") || value=unavailable; frequencies+="${node%/scaling_cur_freq}:$value;"; fi
        done
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$uptime" "${12:-unavailable}" "${13:-unavailable}" \
            "${rss:-unavailable}" "${pss:-unavailable}" "${threads:-unavailable}" "$fds" "${temperatures:-unavailable}" "${frequencies:-unavailable}" >> "$run/resources.tsv"
    }
    deadline=$((SECONDS + seconds))
    while child_is_owned; do
        if [[ -n $secret ]] && grep -qx 'SHELL_READY' "$run/app.log"; then cleanup_secret; fi
        if [[ $SECONDS != "$last_sample" ]]; then sample_resources; last_sample=$SECONDS; fi
        if [[ $supervisor_close == 1 && $close_sent == 0 ]]; then
            if process_matches "$run"; then kill -TERM "$child" 2>/dev/null || true; fi
            printf 'SUPERVISOR_CLOSE_REQUESTED=1\n' >> "$run/runtime.txt"
            close_sent=1
        fi
        if [[ $seconds != 0 && $SECONDS -ge $deadline && $close_sent == 0 ]]; then
            if process_matches "$run"; then
                printf 'SOFT_DEADLINE_CLOSE_REQUESTED=1\n' >> "$run/runtime.txt"
                kill -TERM "$child" 2>/dev/null || true
            fi
            close_sent=1
        fi
        sleep 0.2 || true
    done
    finalize_child
    trap - EXIT TERM INT HUP
    return "$code"
}
status_run() {
    local run=$1
    printf 'RUN_DIR=%s\n' "$run"
    if [[ -f $run/exit_code ]]; then
        cat "$run/result.txt"
    elif process_matches "$run"; then
        printf 'APP_RUNNING=1\n'
        if grep -qx 'SHELL_READY' "$run/app.log"; then printf 'APP_READY=1\n'; else printf 'APP_READY=0\n'; fi
    else
        die 'No matching live process and no actual exit result; inspect service/journal.'; return 1
    fi
    tail -20 "$run/app.log"
}
stop_run() {
    local run=$1 i
    if [[ -f $run/exit_code ]]; then status_run "$run"; return; fi
    process_matches "$run" || { die 'Process identity mismatch; nothing was signalled.'; return 1; }
    kill -TERM "$(cat "$run/app.pid")" 2>/dev/null || true
    for ((i=0; i<60; i++)); do
        if [[ -f $run/exit_code ]]; then status_run "$run"; return "$(cat "$run/exit_code")"; fi
        sleep 0.25
    done
    printf 'CLOSE_PENDING=1; finish or discard the guarded operation on screen, then check status. No SIGKILL sent.\n' >&2
    return 4
}
collect_run() {
    (
        local run=$1 name snapshot completed=0 files=()
        snapshot=$(mktemp -d "$run/.collect.XXXXXXXX")
        trap 'rm -rf -- "$snapshot"' EXIT
        [[ ! -f $run/exit_code ]] || completed=1
        for name in app.log supervisor.log startup_failure.txt runtime.txt resources.tsv release_id app.pid app.start_ticks app.exe boot_id exit_code result.txt kms.json; do
            case $name in exit_code|result.txt) [[ $completed == 1 ]] || continue ;; esac
            if [[ -f $run/$name && ! -L $run/$name ]]; then cp -- "$run/$name" "$snapshot/$name"; files+=("$name"); fi
        done
        # Copy before archiving so an active log cannot make tar fail midway.
        # Explicit allowlist: no database, secret, home or environment dump.
        tar -czf - -C "$snapshot" -- "${files[@]}"
    )
}
main() {
    local action=${1:-} base=${2:-} release run seconds=${3:-1800} session=${4:-}
    valid_base "$base" || return 2
    [[ $EUID != 0 && $(uname -m) == aarch64 ]] || die 'Run this entry on RK as its ordinary user.'
    case $action in
        check) release=$(current_release "$base"); preflight "$base" "$release" ;;
        start)
            [[ $seconds =~ ^(0|[1-9][0-9]{0,4})$ && $seconds -le 86400 ]] || die 'Seconds must be 0..86400 (0 = no soft deadline).'
            case $session in ''|offlineSend|offlineTest|offlineCheck|offlineEngineer|offlineAdmin) ;; *) die 'Unknown anonymous session.' ;; esac
            [[ ! -L $base/lifecycle.lock && ! -L $base/runs && ! -L $base/latest_run ]] || die 'Lifecycle path is a symlink.'
            exec 8>"$base/lifecycle.lock"; flock -n 8 || die 'Application is already running.'
            if systemctl is-active --quiet sn100-shell.service; then die 'Transient graphical service is already active.'; return 1; fi
            release=$(current_release "$base")
            preflight "$base" "$release"
            command -v fuser >/dev/null || die 'fuser is required to verify display ownership.'
            [[ -c /dev/tty7 && -c /dev/dri/card0 ]] || die 'Expected VT/DRM devices are absent.'
            sudo -n true || die 'The display allocator needs the existing noninteractive sudo authorization.'
            local owners owner_status=0
            owners=$(sudo -n fuser /dev/tty7 /dev/dri/card0 2>&1) || owner_status=$?
            if [[ $owner_status != 1 || -n $owners ]]; then
                printf '%s\n' "$owners" >&2
                die 'VT7/DRM are occupied or ownership inspection failed.'
            fi
            run=$base/runs/$(date +%Y%m%d_%H%M%S)_$$
            mkdir -p "$run"
            printf '%s\n' "${run##*/}" > "$base/latest_run"
            sudo -n systemd-run --quiet --collect --unit=sn100-shell --property=Type=exec \
                /usr/bin/openvt -c 7 -s -w -- /usr/sbin/runuser -u "$(id -un)" -- \
                /bin/bash "$release/rk_board.sh" supervise "$base" "$release" "$run" "$seconds" "$session"
            exec 8>&-
            printf 'RUN_DIR=%s\nRELEASE_ID=%s\n' "$run" "${release##*/}"
            local i
            for ((i=0; i<150; i++)); do
                if [[ -f $run/exit_code ]]; then
                    status_run "$run"
                    grep -qx 'SHELL_READY' "$run/app.log" || { printf 'START_FAILED=never_ready\n' >&2; return 1; }
                    return "$(cat "$run/exit_code")"
                fi
                if [[ -f $run/app.log ]] && grep -qx 'SHELL_READY' "$run/app.log" && process_matches "$run"; then
                    printf 'APP_READY=1\n'; return 0
                fi
                sleep 0.2
            done
            printf 'Startup did not become ready within 30s; requesting guarded close.\n' >&2
            if [[ -f $run/supervisor.log ]]; then tail -20 "$run/supervisor.log" >&2; fi
            if process_matches "$run"; then kill -TERM "$(cat "$run/app.pid")" 2>/dev/null || true; fi
            return 1 ;;
        supervise)
            [[ $# == 6 && $3 == "$base/releases/"* && $4 == "$base/runs/"* ]] || die 'Invalid supervisor arguments.'
            run=$4
            [[ ${run#"$base/runs/"} =~ ^[0-9_]+$ && $(readlink -f "$run") == "$run" ]] || die 'Unsafe supervisor run path.'
            exec > "$run/supervisor.log" 2>&1
            trap 'startup_status=$?; if [[ ! -f $run/app.pid ]]; then printf "SUPERVISOR_START_FAILED=%s\n" "$startup_status" > "$run/startup_failure.txt"; fi' EXIT
            supervise "$base" "$3" "$4" "$5" "$6" ;;
        status|stop|collect)
            run=$(latest_run "$base")
            case $action in status) status_run "$run" ;; stop) stop_run "$run" ;; collect) collect_run "$run" ;; esac ;;
        *) die 'Usage: rk_board.sh {check|start|status|stop|collect} BASE [SECONDS [ANONYMOUS_SESSION]]'; return 2 ;;
    esac
}
if [[ ${BASH_SOURCE[0]} == "$0" ]]; then main "$@"; fi
