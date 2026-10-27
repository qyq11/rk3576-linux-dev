#!/usr/bin/env bash
# Offline host/lifecycle regression. Native fixtures are never RK/ARM evidence.
set -Eeuo pipefail
test_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
program_dir=$(cd -- "$test_dir/.." && pwd)
root=$(cd -- "$program_dir/../.." && pwd)
work=${SN100_WORKFLOW_TEST_DIR:-$root/temp/sn100_host/rk_prepare/workflow_tests}
[[ $work == /* ]] || { echo 'SN100_WORKFLOW_TEST_DIR must be absolute.' >&2; exit 2; }
[[ $EUID != 0 && -d /proc/self ]] || { echo 'Run on Linux as the ordinary WSL user.' >&2; exit 2; }
mkdir -p -- "$work"
run_dir=$(mktemp -d "$work/run_XXXXXXXX")
mkdir -p -- "$run_dir/cases"
fixture=$run_dir/rk_lifecycle_fixture
gcc -std=c11 -Wall -Wextra -Werror -O2 "$test_dir/fixtures/rk_lifecycle_fixture.c" -o "$fixture"
board=$run_dir/board_under_test.sh
cp -- "$program_dir/shell/rk_board.sh" "$board"
declare -A owned_fixture_start=() owned_fixture_exe=()
passed=0
failed=0
test_filter=${RK_WORKFLOW_TEST_FILTER:-}
case ${1:-} in
    '') ;;
    --publication-only) test_filter='release_|collect|mock_remote' ;;
    *) echo 'Usage: rk_deployment_test.sh [--publication-only]' >&2; exit 2 ;;
esac

proc_start()
{
    local contents
    contents=$(cat "/proc/$1/stat") || return
    contents=${contents##*) }
    read -r -a fields <<< "$contents"
    printf '%s\n' "${fields[19]}"
}

remember_fixture()
{
    owned_fixture_start[$1]=$(proc_start "$1")
    owned_fixture_exe[$1]=$(readlink -f "/proc/$1/exe")
}

cleanup()
{
    local pid start
    for pid in "${!owned_fixture_start[@]}"; do
        start=$(proc_start "$pid" 2>/dev/null) || continue
        if [[ $start == "${owned_fixture_start[$pid]}" && $(readlink -f "/proc/$pid/exe" 2>/dev/null) == "${owned_fixture_exe[$pid]}" ]]; then
            kill -KILL "$pid" 2>/dev/null || :
            wait "$pid" 2>/dev/null || :
        fi
    done
}
trap cleanup EXIT

assert()
{
    "$@" || { printf 'ASSERTION FAILED:'; printf ' %q' "$@"; printf '\n'; return 1; }
}

expect_failure()
{
    local result=0
    "$@" || result=$?
    [[ $result -ne 0 ]] || { echo 'Expected a failure, received exit 0.' >&2; return 1; }
}

wait_for_file()
{
    local attempt
    for ((attempt=0; attempt<100; ++attempt)); do
        [[ -s $1 ]] && return 0
        sleep 0.05
    done
    echo "Timed out waiting for $1" >&2
    return 1
}

run_test()
{
    local name=$1 result=0
    shift
    [[ -z $test_filter || $name =~ $test_filter ]] || return 0
    ( trap cleanup EXIT; "$@" ) > "$run_dir/cases/$name.log" 2>&1 || result=$?
    if [[ $result == 0 ]]; then
        passed=$((passed + 1))
        printf 'PASS %s\n' "$name"
        printf '%s\tPASS\n' "$name" >> "$run_dir/results.tsv"
    else
        failed=$((failed + 1))
        printf 'FAIL %s exit=%s (see %s)\n' "$name" "$result" "$run_dir/cases/$name.log"
        printf '%s\tFAIL\t%s\n' "$name" "$result" >> "$run_dir/results.tsv"
        cat -- "$run_dir/cases/$name.log"
    fi
}

fixture_contract()
{
    local result=0
    "$fixture" --mode normal > "$run_dir/fixture_normal.log" || return
    assert grep -q '^SHELL_READY' "$run_dir/fixture_normal.log" || return
    assert grep -q '^THREADS_STOPPED true' "$run_dir/fixture_normal.log" || return
    "$fixture" --mode nonzero > "$run_dir/fixture_nonzero.log" || result=$?
    assert test "$result" -eq 23 || return
    assert grep -q '^SHELL_READY' "$run_dir/fixture_nonzero.log" || return
    result=0
    "$fixture" --mode early-failure > "$run_dir/fixture_early_failure.log" || result=$?
    assert test "$result" -eq 29 || return
    assert test "$(grep -c '^SHELL_READY' "$run_dir/fixture_early_failure.log" || :)" -eq 0
}

board_call()
{
    ( source "$board"; "$@" )
}

make_release()
{
    local name=$1 staging identity
    base=$run_dir/$name
    staging=$base/releases/staging
    app_run=$base/runs/20261023_100000_1
    mkdir -p -- "$staging" "$app_run"
    cp -- "$fixture" "$staging/sn100_shell"
    cp -- "$board" "$staging/rk_board.sh"
    (cd -- "$staging" && sha256sum sn100_shell rk_board.sh > SHA256SUMS)
    identity=$(sha256sum "$staging/SHA256SUMS" | cut -d' ' -f1)
    printf '%s\n' "$identity" > "$staging/release_id"
    release=$base/releases/$identity
    # Directories on /mnt/c can be held briefly by Windows indexers. The test
    # setup needs a hash-named release, not the product's atomic publication.
    mkdir -p -- "$release"
    cp -- "$staging/"* "$release/"
}

wait_for_log()
{
    local attempt
    for ((attempt=0; attempt<100; ++attempt)); do
        if [[ -f $2 ]] && grep -q -- "$1" "$2"; then return 0; fi
        sleep 0.05
    done
    echo "Timed out waiting for $1 in $2" >&2
    return 1
}

launch_supervisor()
{
    local mode=$1 seconds=${2:-0}
    (
        export RK_LIFECYCLE_MODE=$mode
        source "$board"
        supervise "$base" "$release" "$app_run" "$seconds" ''
    ) > "$app_run/supervisor.log" 2>&1 &
    supervisor_pid=$!
    wait_for_file "$app_run/app.start_ticks" || return
    fixture_pid=$(cat "$app_run/app.pid")
    remember_fixture "$fixture_pid" || return
    wait_for_log '^SHELL_READY$' "$app_run/app.log"
}

source_has_no_main()
{
    board_call true
}

native_cli_refuses_board_actions()
{
    make_release architecture_boundary || return
    [[ $(uname -m) != aarch64 ]] || { echo 'This regression suite requires its native x86_64 fixture environment.' >&2; return 1; }
    expect_failure bash "$board" check "$base" || return
    assert test ! -e "$app_run/app.pid"
}

release_integrity()
{
    make_release integrity || return
    board_call verify_release "$release" || return
    printf 'changed' >> "$release/sn100_shell"
    expect_failure board_call verify_release "$release" || return
    assert test ! -f "$app_run/app.pid"
}

release_unlisted_and_symlink_pollution()
{
    make_release unlisted || return
    mkdir -p -- "$release/lib"
    printf 'unmanifested anonymous sentinel\n' > "$release/lib/extra.so"
    expect_failure board_call verify_release "$release" || return
    make_release release_symlink || return
    ln -s sn100_shell "$release/unlisted_link"
    expect_failure board_call verify_release "$release"
}

report_release_requires_helper_and_fonts()
{
    local mode staging identity name
    for mode in complete missing_helper missing_fonts; do
        make_release "report_$mode" || return
        staging=$base/report_stage
        mkdir -p "$staging/fonts"
        mkdir -p "$staging/plugins/platforms"
        printf 'native plugin metadata fixture; not a real plugin\n' > "$staging/plugins/platforms/libqoffscreen.so"
        cp -- "$fixture" "$staging/sn100_shell"
        cp -- "$board" "$staging/rk_board.sh"
        printf '%s\n' '{"capabilities":["report_preview_pdf"]}' > "$staging/release.json"
        if [[ $mode != missing_helper ]]; then cp -- "$fixture" "$staging/sn100_report_renderer"; fi
        if [[ $mode != missing_fonts ]]; then
            for name in NotoSansCJK-Regular.ttc NotoSansCJK-Bold.ttc fonts.conf LICENSE.txt; do
                printf 'native metadata fixture; not a real font\n' > "$staging/fonts/$name"
            done
        fi
        (cd "$staging" && sha256sum sn100_shell rk_board.sh release.json plugins/platforms/libqoffscreen.so > SHA256SUMS
            if [[ -f sn100_report_renderer ]]; then sha256sum sn100_report_renderer >> SHA256SUMS; fi
            if [[ -f fonts/fonts.conf ]]; then sha256sum fonts/* >> SHA256SUMS; fi)
        identity=$(sha256sum "$staging/SHA256SUMS" | cut -d' ' -f1)
        printf '%s\n' "$identity" > "$staging/release_id"
        release=$base/releases/$identity
        mkdir -p "$release"
        cp -a -- "$staging/." "$release/"
        if [[ $mode == complete ]]; then
            board_call verify_release "$release" || return
            # Run the actual font-environment step called by preflight, with
            # hardware/ldd checks outside this native lifecycle fixture scope.
            assert test ! -e "$base/runtime" || return
            board_call runtime_environment "$release" || return
            assert test -d "$base/preflight_cache" || return
            assert test ! -e "$base/runtime" || return
            RK_LIFECYCLE_MODE=normal board_call supervise "$base" "$release" "$app_run" 0 '' || return
            assert test "$(cat "$app_run/exit_code")" = 0 || return
            assert grep -qx 'CLEAN_SHUTDOWN=1' "$app_run/result.txt" || return
        else
            expect_failure board_call verify_release "$release" || return
        fi
    done
}

sample_release_requires_svg_plugin()
{
    local mode staging identity
    for mode in complete missing_plugin; do
        make_release "sample_$mode" || return
        staging=$base/sample_stage
        mkdir -p "$staging/plugins/imageformats"
        cp -- "$fixture" "$staging/sn100_shell"
        cp -- "$board" "$staging/rk_board.sh"
        printf '%s\n' '{"capabilities":["sample_local_presets","sample_local_tasks"]}' > "$staging/release.json"
        if [[ $mode == complete ]]; then
            printf 'native fixture only; not an ARM plugin\n' > "$staging/plugins/imageformats/libqsvg.so"
        fi
        (cd "$staging" && sha256sum sn100_shell rk_board.sh release.json > SHA256SUMS
            if [[ -f plugins/imageformats/libqsvg.so ]]; then sha256sum plugins/imageformats/libqsvg.so >> SHA256SUMS; fi)
        identity=$(sha256sum "$staging/SHA256SUMS" | cut -d' ' -f1)
        printf '%s\n' "$identity" > "$staging/release_id"
        release=$base/releases/$identity
        mkdir -p "$release"
        cp -a -- "$staging/." "$release/"
        if [[ $mode == complete ]]; then board_call verify_release "$release" || return
        else expect_failure board_call verify_release "$release" || return; fi
    done
}

identity_and_forged_metadata()
{
    local real_ticks real_exe real_boot real_pid
    make_release identity || return
    launch_supervisor wait || return
    board_call process_matches "$app_run" || return
    real_ticks=$(cat "$app_run/app.start_ticks")
    assert test "$(board_call start_ticks "$fixture_pid")" = "$(proc_start "$fixture_pid")" || return
    # A comm with a closing parenthesis and a space proves real /proc parsing.
    assert grep -q '(fixture) worker)' "/proc/$fixture_pid/stat" || return
    printf '%s\n' "$((real_ticks + 1))" > "$app_run/app.start_ticks"
    expect_failure board_call process_matches "$app_run" || return
    expect_failure board_call stop_run "$app_run" || return
    assert kill -0 "$fixture_pid" || return
    printf '%s\n' "$real_ticks" > "$app_run/app.start_ticks"
    real_exe=$(cat "$app_run/app.exe")
    printf '/a/different/executable\n' > "$app_run/app.exe"
    expect_failure board_call process_matches "$app_run" || return
    printf '%s\n' "$real_exe" > "$app_run/app.exe"
    real_boot=$(cat "$app_run/boot_id")
    printf '00000000-0000-0000-0000-000000000000\n' > "$app_run/boot_id"
    expect_failure board_call process_matches "$app_run" || return
    printf '%s\n' "$real_boot" > "$app_run/boot_id"
    real_pid=$(cat "$app_run/app.pid")
    printf '$(touch %s)\n' "$run_dir/metadata_executed" > "$app_run/app.pid"
    expect_failure board_call stop_run "$app_run" || return
    assert test ! -e "$run_dir/metadata_executed" || return
    assert kill -0 "$fixture_pid" || return
    printf '%s\n' "$real_pid" > "$app_run/app.pid"
    board_call stop_run "$app_run" || return
    wait "$supervisor_pid" || return
    assert test "$(cat "$app_run/exit_code")" = 0
}

normal_and_nonzero_exit()
{
    local result=0
    make_release normal || return
    RK_LIFECYCLE_MODE=normal board_call supervise "$base" "$release" "$app_run" 0 '' || return
    assert test "$(cat "$app_run/exit_code")" = 0 || return
    assert grep -qx 'CLEAN_SHUTDOWN=1' "$app_run/result.txt" || return
    assert_resource_samples "$app_run/resources.tsv" 1 || return
    assert test "$(find "$app_run/runtime" -name 'bootstrap.*' -type f | wc -l)" = 0 || return
    make_release nonzero || return
    RK_LIFECYCLE_MODE=nonzero board_call supervise "$base" "$release" "$app_run" 0 '' || result=$?
    assert test "$result" = 23 || return
    assert test "$(cat "$app_run/exit_code")" = 23 || return
    assert grep -qx 'CLEAN_SHUTDOWN=0' "$app_run/result.txt" || return
    assert_resource_samples "$app_run/resources.tsv" 1 || return
    assert test "$(find "$app_run/runtime" -name 'bootstrap.*' -type f | wc -l)" = 0
}

assert_resource_samples()
{
    local resource_file=$1 minimum_rows=$2
    assert test -f "$resource_file" || return
    assert awk -F '\t' -v minimum="$minimum_rows" '
        NR == 1 {
            if (NF != 9 || $1 != "uptime_s" || $2 != "utime_ticks" || $3 != "stime_ticks" ||
                $4 != "rss_kib" || $5 != "pss_kib" || $6 != "threads" || $7 != "fd_count" ||
                $8 != "temperatures_mC" || $9 != "frequencies_kHz") exit 1
            next
        }
        {
            if (NF != 9 || $1 !~ /^[0-9]+([.][0-9]+)?$/) exit 1
            for (field = 2; field <= 7; ++field)
                if ($field != "unavailable" && $field !~ /^[0-9]+$/) exit 1
            ++rows
        }
        END { if (NR < 1 || rows < minimum) exit 1 }
    ' "$resource_file"
}

early_failure_removes_secret()
{
    local result=0
    make_release early_failure || return
    RK_LIFECYCLE_MODE=early-failure board_call supervise "$base" "$release" "$app_run" 0 '' || result=$?
    assert test "$result" = 29 || return
    assert test "$(cat "$app_run/exit_code")" = 29 || return
    # An application can disappear before its first sample; the header remains
    # valid and the genuine nonzero wait result must survive that /proc race.
    assert_resource_samples "$app_run/resources.tsv" 0 || return
    assert test "$(find "$app_run/runtime" -name 'bootstrap.*' -type f | wc -l)" = 0
}

zero_exit_before_ready_is_observable()
{
    make_release early_clean || return
    RK_LIFECYCLE_MODE=early-clean board_call supervise "$base" "$release" "$app_run" 0 '' || return
    assert test "$(cat "$app_run/exit_code")" = 0 || return
    assert grep -qx 'CLEAN_SHUTDOWN=0' "$app_run/result.txt" || return
    assert_resource_samples "$app_run/resources.tsv" 0 || return
    assert test "$(grep -c '^SHELL_READY$' "$app_run/app.log" || :)" = 0 || return
    assert test "$(find "$app_run/runtime" -name 'bootstrap.*' -type f | wc -l)" = 0
}

supervisor_term_is_guarded_close()
{
    make_release supervisor_term || return
    launch_supervisor wait || return
    kill -TERM "$supervisor_pid" || return
    wait_for_file "$app_run/exit_code" || return
    wait "$supervisor_pid" || return
    assert test "$(cat "$app_run/exit_code")" = 0 || return
    assert grep -qx 'CLEAN_SHUTDOWN=1' "$app_run/result.txt" || return
    assert test "$(find "$app_run/runtime" -name 'bootstrap.*' -type f | wc -l)" = 0
}

ready_cleanup_and_soft_deadline()
{
    local attempt
    make_release soft_deadline || return
    launch_supervisor wait 1 || return
    for ((attempt=0; attempt<50; ++attempt)); do
        [[ -z $(find "$app_run/runtime" -name 'bootstrap.*' -type f -print -quit) ]] && break
        sleep 0.02
    done
    assert test -z "$(find "$app_run/runtime" -name 'bootstrap.*' -type f -print -quit)" || return
    wait "$supervisor_pid" || return
    assert test "$(cat "$app_run/exit_code")" = 0 || return
    assert grep -qx 'SOFT_DEADLINE_CLOSE_REQUESTED=1' "$app_run/runtime.txt"
}

guarded_stop_never_kills()
{
    local result=0
    make_release guard || return
    launch_supervisor reject-stop || return
    board_call stop_run "$app_run" || result=$?
    assert test "$result" = 4 || return
    assert kill -0 "$fixture_pid" || return
    assert test ! -e "$app_run/exit_code" || return
    assert grep -q '^SHUTDOWN_REJECTED' "$app_run/app.log" || return
    # End only this test's identity-checked native fixture; the product sent no KILL.
    board_call process_matches "$app_run" || return
    kill -KILL "$fixture_pid" || return
    result=0
    wait "$supervisor_pid" || result=$?
    assert test "$result" = 137 || return
    assert test "$(cat "$app_run/exit_code")" = 137
}

existing_runtime_is_preserved()
{
    make_release unmarked_runtime || return
    mkdir -p -- "$base/runtime/data/account"
    printf 'existing anonymous sentinel\n' > "$base/runtime/data/account/existing.txt"
    expect_failure board_call supervise "$base" "$release" "$app_run" 0 '' || return
    assert grep -qx 'existing anonymous sentinel' "$base/runtime/data/account/existing.txt" || return
    assert test ! -e "$app_run/app.pid" || return
    printf 'sn100-offline-runtime-v1\n' > "$base/runtime/offline-runtime.tag"
    expect_failure board_call supervise "$base" "$release" "$app_run" 0 '' || return
    assert test ! -e "$app_run/app.pid" || return
    assert grep -qx 'existing anonymous sentinel' "$base/runtime/data/account/existing.txt"
}

collection_is_allowlisted()
{
    local archive=$run_dir/collected.tar.gz members
    make_release collect || return
    RK_LIFECYCLE_MODE=normal board_call supervise "$base" "$release" "$app_run" 0 '' || return
    printf 'anonymous DB sentinel\n' > "$app_run/patient.db"
    printf 'never collect sentinel\n' > "$app_run/bootstrap.secret"
    printf 'unrelated file\n' > "$app_run/extra.log"
    printf 'anonymous supervisor collection sentinel\n' > "$app_run/supervisor.log"
    printf 'anonymous startup failure collection sentinel\n' > "$app_run/startup_failure.txt"
    # Even an allowed name must be excluded when it is a symlink.
    mv -- "$app_run/kms.json" "$app_run/private_kms.json"
    ln -s private_kms.json "$app_run/kms.json"
    board_call collect_run "$app_run" > "$archive" || return
    members=$(tar -tzf "$archive") || return
    printf '%s\n' "$members" > "$run_dir/collected_members.txt"
    assert grep -qx 'app.log' "$run_dir/collected_members.txt" || return
    assert grep -qx 'result.txt' "$run_dir/collected_members.txt" || return
    assert grep -qx 'resources.tsv' "$run_dir/collected_members.txt" || return
    assert grep -qx 'supervisor.log' "$run_dir/collected_members.txt" || return
    assert grep -qx 'startup_failure.txt' "$run_dir/collected_members.txt" || return
    assert test "$(grep -Ev '^(app.log|supervisor.log|startup_failure.txt|runtime.txt|resources.tsv|release_id|app.pid|app.start_ticks|app.exe|boot_id|exit_code|result.txt|kms.json)$' "$run_dir/collected_members.txt" | wc -l)" = 0 || return
    assert test "$(grep -Ec '(^|/)(patient.db|bootstrap.secret|extra.log|kms.json|private_kms.json|runtime)(/|$)' "$run_dir/collected_members.txt" || :)" = 0 || return
    assert test "$(tar -xOzf "$archive" exit_code)" = 0
}

active_collection_takes_private_snapshot()
{
    local archive members iteration before after
    make_release active_collect || return
    launch_supervisor active-log || return
    printf 'anonymous DB sentinel\n' > "$app_run/patient.db"
    printf 'anonymous secret sentinel\n' > "$app_run/bootstrap.secret"
    printf 'uncommitted result must not be collected\n' > "$app_run/result.txt"
    # The fixture itself writes an initial 1 MiB and keeps app.log changing;
    # another writer would interfere with its ordinary non-O_APPEND stdout FD.
    before=$(stat -c %s "$app_run/app.log")
    for iteration in 1 2 3; do
        archive=$run_dir/active_collect_$iteration.tar.gz
        board_call collect_run "$app_run" > "$archive" || return
        assert gzip -t "$archive" || return
        members=$(tar -tzf "$archive") || return
        printf '%s\n' "$members" > "$run_dir/active_collect_$iteration.members"
        assert grep -qx app.log "$run_dir/active_collect_$iteration.members" || return
        assert grep -qx resources.tsv "$run_dir/active_collect_$iteration.members" || return
        assert test "$(grep -Ec '^(patient.db|bootstrap.secret|result.txt|exit_code|runtime)(/|$)' "$run_dir/active_collect_$iteration.members" || :)" = 0 || return
        assert test -z "$(find "$app_run" -maxdepth 1 -type d -name '.collect.*' -print -quit)" || return
        assert board_call process_matches "$app_run" || return
        sleep 0.08
    done
    after=$(stat -c %s "$app_run/app.log")
    assert test "$after" -gt "$before" || return
    board_call stop_run "$app_run" || return
    wait "$supervisor_pid" || return
    assert test "$(cat "$app_run/exit_code")" = 0
}

failed_build_never_contacts_target()
{
    local isolated=$run_dir/failed_build_repository module fake_work action result trace network
    module=$isolated/06_programs/sn100_host/shell
    fake_work=$isolated/temp/sn100_host/rk_prepare
    trace=$run_dir/failed_build_trace.txt
    network=$run_dir/network_attempts.txt
    mkdir -p -- "$module" "$fake_work/build" "$isolated/sentinel_bin"
    cp -- "$program_dir/shell/rk.sh" "$module/rk.sh"
    cat > "$module/package_arm64.py" <<'PY'
import os
import sys
with open(os.environ["RK_WORKFLOW_TEST_TRACE"], "a", encoding="utf-8") as log:
    log.write("snapshot\n" if len(sys.argv) > 1 and sys.argv[1] == "snapshot" else "UNEXPECTED_PACKAGE\n")
PY
    cat > "$module/prepare_runtime.sh" <<'SH'
#!/usr/bin/env bash
printf 'runtime\n' >> "$RK_WORKFLOW_TEST_TRACE"
SH
    cat > "$module/build.sh" <<'SH'
#!/usr/bin/env bash
printf 'build-failed\n' >> "$RK_WORKFLOW_TEST_TRACE"
exit 37
SH
    cat > "$isolated/sentinel_bin/ssh" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "${0##*/}" >> "$RK_WORKFLOW_TEST_NETWORK"
exit 99
SH
    chmod +x "$isolated/sentinel_bin/ssh"
    for action in scp curl wget; do cp -- "$isolated/sentinel_bin/ssh" "$isolated/sentinel_bin/$action"; done
    printf 'old ARM artifact sentinel (never execute)\n' > "$fake_work/build/sn100_shell"
    for action in prepare deploy; do
        : > "$trace"
        : > "$network"
        result=0
        RK_WORKFLOW_TEST_TRACE=$trace RK_WORKFLOW_TEST_NETWORK=$network \
            PATH="$isolated/sentinel_bin:$PATH" bash "$module/rk.sh" "$action" || result=$?
        assert test "$result" = 37 || return
        assert test "$(cat "$trace")" = $'snapshot\nruntime\nbuild-failed' || return
        assert test ! -s "$network" || return
        assert grep -qx 'old ARM artifact sentinel (never execute)' "$fake_work/build/sn100_shell" || return
        assert test ! -e "$fake_work/packages/latest_package" || return
    done
}

mock_remote_publication_preserves_current()
{
    local isolated=$run_dir/mock_remote_repository module fake_work mock_bin remote_bin trace
    local package_a package_b payload_a payload_b id_a id_b deployed result original_current version
    module=$isolated/06_programs/sn100_host/shell
    fake_work=$isolated/temp/sn100_host/rk_prepare
    mock_bin=$isolated/mock_transport_bin
    remote_bin=$isolated/mock_target_bin
    trace=$isolated/mock_trace.txt
    deployed=$isolated/mock_target/sn100_shell
    mkdir -p -- "$module" "$mock_bin" "$remote_bin" "$isolated/mock_packages"
    cp -- "$program_dir/shell/rk.sh" "$module/rk.sh"
    # Only the isolated test prepare phase is substituted. The driver and its
    # remote publication shell remain verbatim production source.
    cat > "$module/prepare_runtime.sh" <<'SH'
#!/usr/bin/env bash
mkdir -p -- "$1"
printf 'MOCK_NATIVE_RUNTIME\n' >> "$RK_WORKFLOW_MOCK_TRACE"
SH
    cat > "$module/build.sh" <<'SH'
#!/usr/bin/env bash
cp -- "$RK_WORKFLOW_MOCK_FIXTURE" "$1"
printf 'MOCK_NATIVE_BUILD_NO_ARM_VALIDATION\n' >> "$RK_WORKFLOW_MOCK_TRACE"
SH
    cat > "$module/check_arm64.py" <<'PY'
print("MOCK_NATIVE_CONTROL_FLOW_ONLY: no ARM architecture or dependency validation")
PY
    cat > "$module/package_arm64.py" <<'PY'
import json
import os
from pathlib import Path
import shutil
import sys
destination = Path(sys.argv[sys.argv.index("--output") + 1])
if sys.argv[1] == "snapshot":
    destination.write_text(json.dumps({"native_mock_control_flow_only": True}), encoding="utf-8")
else:
    destination.mkdir(parents=True, exist_ok=True)
    source = Path(os.environ["RK_WORKFLOW_MOCK_PACKAGE"])
    package = destination / source.name
    shutil.copyfile(source, package)
    (destination / "latest_package").write_text(str(package) + "\n", encoding="utf-8")
PY
    cat > "$mock_bin/ssh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
[[ ${@: -2:1} == test@offline.mock ]] || exit 96
printf 'MOCK_SSH_LOCAL_EXECUTION\n' >> "$RK_WORKFLOW_MOCK_TRACE"
command=${!#}
PATH="$RK_WORKFLOW_MOCK_REMOTE_BIN:/usr/bin:/bin" bash -c "$command"
SH
    cat > "$mock_bin/scp" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
source=${@: -2:1}
target=${!#}
[[ $target == test@offline.mock:/* ]] || exit 96
printf 'MOCK_SCP_LOCAL_COPY\n' >> "$RK_WORKFLOW_MOCK_TRACE"
cp -- "$source" "${target#test@offline.mock:}"
SH
    cat > "$remote_bin/uname" <<'SH'
#!/usr/bin/env bash
if [[ ${1:-} == -m ]]; then
    printf 'MOCK_TARGET_ARCH_GATE_ONLY_NO_ARM_EXECUTION\n' >> "$RK_WORKFLOW_MOCK_TRACE"
    printf 'aarch64\n'
else
    /usr/bin/uname "$@"
fi
SH
    cat > "$remote_bin/systemctl" <<'SH'
#!/usr/bin/env bash
[[ ${1:-} == is-active ]] || exit 97
printf 'MOCK_SERVICE_INACTIVE\n' >> "$RK_WORKFLOW_MOCK_TRACE"
exit 3
SH
    cat > "$remote_bin/ldd" <<'SH'
#!/usr/bin/env bash
printf 'NATIVE_LDD %s\n' "$1" >> "$RK_WORKFLOW_MOCK_TRACE"
/usr/bin/ldd "$@"
SH
    chmod +x "$mock_bin/ssh" "$mock_bin/scp" "$remote_bin/uname" "$remote_bin/systemctl" "$remote_bin/ldd"
    for version in a b; do
        local payload=$isolated/payload_$version manifest_id package
        mkdir -p -- "$payload/plugins/sqldrivers"
        cp -- "$fixture" "$payload/sn100_shell"
        cp -- "$fixture" "$payload/plugins/sqldrivers/libqsqlite.so"
        cp -- "$board" "$payload/rk_board.sh"
        printf '{"native_mock_control_flow_only":true,"version":"%s"}\n' "$version" > "$payload/release.json"
        (cd -- "$payload" && sha256sum sn100_shell rk_board.sh release.json plugins/sqldrivers/libqsqlite.so > SHA256SUMS)
        manifest_id=$(sha256sum "$payload/SHA256SUMS" | cut -d' ' -f1)
        printf '%s\n' "$manifest_id" > "$payload/release_id"
        package=$isolated/mock_packages/sn100_shell_$manifest_id.tar.gz
        tar -czf "$package" -C "$payload" . || return
        if [[ $version == a ]]; then package_a=$package; payload_a=$payload; id_a=$manifest_id
        else package_b=$package; payload_b=$payload; id_b=$manifest_id; fi
    done
    mock_deploy()
    {
        RK_HOST=test@offline.mock RK_REMOTE_ROOT=$isolated/mock_target \
            RK_WORKFLOW_MOCK_TRACE=$trace RK_WORKFLOW_MOCK_FIXTURE=$fixture \
            RK_WORKFLOW_MOCK_PACKAGE=$1 RK_WORKFLOW_MOCK_REMOTE_BIN=$remote_bin \
            PATH="$mock_bin:$PATH" bash "$module/rk.sh" deploy
    }
    mock_deploy "$package_a" || return
    assert test "$(readlink "$deployed/current")" = "releases/$id_a" || return
    assert test ! -e "$deployed/previous_release" || return
    assert board_call verify_release "$deployed/releases/$id_a" || return
    mock_deploy "$package_b" || return
    assert test "$(readlink "$deployed/current")" = "releases/$id_b" || return
    assert test "$(cat "$deployed/previous_release")" = "releases/$id_a" || return
    assert board_call verify_release "$deployed/releases/$id_a" || return
    assert board_call verify_release "$deployed/releases/$id_b" || return
    assert grep -q 'NATIVE_LDD .*plugins/sqldrivers/libqsqlite.so$' "$trace" || return
    original_current=$(readlink "$deployed/current")
    # The old sha256sum -c-only implementation would accept this internally
    # consistent replacement under the original release directory identity.
    printf '{"native_mock_control_flow_only":true,"tampered":true}\n' > "$deployed/releases/$id_a/release.json"
    (cd -- "$deployed/releases/$id_a" && sha256sum sn100_shell rk_board.sh release.json plugins/sqldrivers/libqsqlite.so > SHA256SUMS)
    local changed_id
    changed_id=$(sha256sum "$deployed/releases/$id_a/SHA256SUMS" | cut -d' ' -f1)
    printf '%s\n' "$changed_id" > "$deployed/releases/$id_a/release_id"
    (cd -- "$deployed/releases/$id_a" && sha256sum --quiet -c SHA256SUMS) || return
    result=0
    mock_deploy "$package_a" || result=$?
    assert test "$result" -ne 0 || return
    assert test "$(readlink "$deployed/current")" = "$original_current" || return
    assert test "$(cat "$deployed/previous_release")" = "releases/$id_a" || return
    assert test -z "$(find "$deployed" -maxdepth 1 -name '.upload.*' -print -quit)" || return
    assert test -z "$(find "$deployed/releases" -maxdepth 1 -name '.stage.*' -print -quit)" || return
    cp -a -- "$payload_a/." "$deployed/releases/$id_a/"
    assert board_call verify_release "$deployed/releases/$id_a" || return
    printf 'unlisted native pollution sentinel\n' > "$deployed/releases/$id_a/extra.so"
    (cd -- "$deployed/releases/$id_a" && sha256sum --quiet -c SHA256SUMS) || return
    result=0
    mock_deploy "$package_a" || result=$?
    assert test "$result" -ne 0 || return
    assert test "$(readlink "$deployed/current")" = "$original_current" || return
    assert test "$(cat "$deployed/previous_release")" = "releases/$id_a" || return
    assert test -z "$(find "$deployed" -maxdepth 1 -name '.upload.*' -print -quit)" || return
    assert test -z "$(find "$deployed/releases" -maxdepth 1 -name '.stage.*' -print -quit)" || return
    assert board_call verify_release "$deployed/releases/$id_b" || return
    assert test ! -e "$deployed/runs" || return
    assert test -z "$(find "$deployed" -maxdepth 1 -name '.current.*' -print -quit)" || return
    printf 'MOCK_PUBLICATION_ONLY=1\nREAL_NETWORK_CALLS=0\nARM_VALIDATION_PERFORMED=0\n'
}

printer_settings_capability_controls_startup()
{
    local option expected identity staging
    for option in legacy enabled; do
        make_release "printer_$option" || return
        staging=$base/releases/staging
        expected=0
        if [[ $option == enabled ]]; then
            expected=1
            printf '%s\n' '{"capabilities":["local_printer_settings"]}' > "$staging/release.json"
        else
            printf '%s\n' '{"capabilities":[]}' > "$staging/release.json"
        fi
        (cd "$staging" && sha256sum sn100_shell rk_board.sh release.json > SHA256SUMS)
        identity=$(sha256sum "$staging/SHA256SUMS" | cut -d' ' -f1)
        printf '%s\n' "$identity" > "$staging/release_id"
        release=$base/releases/$identity
        mkdir -p -- "$release"
        cp -- "$staging/"* "$release/"
        board_call verify_release "$release" || return
        RK_LIFECYCLE_MODE=normal board_call supervise "$base" "$release" "$app_run" 0 '' || return
        assert grep -qx "PRINTER_SETTINGS_OPT_IN=$expected" "$app_run/app.log" || return
        assert test "$(cat "$app_run/exit_code")" = 0 || return
    done
}

run_test native_fixture_contract fixture_contract
run_test printer_settings_capability_controls_startup printer_settings_capability_controls_startup
run_test source_does_not_execute_main source_has_no_main
run_test native_cli_refuses_board_actions native_cli_refuses_board_actions
run_test release_sha_tampering_rejected release_integrity
run_test release_extra_file_and_symlink_rejected release_unlisted_and_symlink_pollution
run_test release_report_helper_and_font_contract report_release_requires_helper_and_fonts
run_test release_sample_svg_plugin_contract sample_release_requires_svg_plugin
run_test real_proc_and_forged_identity identity_and_forged_metadata
run_test normal_and_nonzero_exit normal_and_nonzero_exit
run_test early_failure_secret_cleanup early_failure_removes_secret
run_test zero_exit_without_ready_is_observable zero_exit_before_ready_is_observable
run_test supervisor_term_uses_guarded_close supervisor_term_is_guarded_close
run_test ready_secret_cleanup_and_soft_deadline ready_cleanup_and_soft_deadline
run_test guarded_stop_has_no_forced_kill guarded_stop_never_kills
run_test existing_runtime_is_preserved existing_runtime_is_preserved
run_test collect_allowlist collection_is_allowlisted
run_test active_collect_uses_private_snapshot active_collection_takes_private_snapshot
run_test failed_build_blocks_prepare_and_deploy_network failed_build_never_contacts_target
run_test mock_remote_publication_preserves_current mock_remote_publication_preserves_current
printf 'WORKFLOW_TEST_RESULT passed=%s failed=%s environment=WSL_native_only logs=%s\n' "$passed" "$failed" "$run_dir"
[[ $failed == 0 ]]
