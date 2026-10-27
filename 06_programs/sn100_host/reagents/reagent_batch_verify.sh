#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID != 0 ]] || { echo 'Use ordinary WSL user qyq.' >&2; exit 2; }
src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$src/../../.." && pwd)
out="$root/temp/sn100_host/projects_remaining_20261025/reagents"
mode=${1:-test}
[[ $# == 0 ]] || shift
case "$mode" in
  build|test) project="$src/tests/reagent_batch_test.pro"; target=reagent_batch_test; tree=batch_build ;;
  basis) project="$src/tests/tests.pro"; target=test_reagents; tree=basis_build ;;
  standalone) project="$src/reagents.pro"; target=sn100_reagents; tree=standalone_build ;;
  *) echo 'Usage: reagent_batch_verify.sh build|test|basis|standalone [test/app arguments]' >&2; exit 2 ;;
esac
mkdir -p "$out/$tree" "$out/logs" "$out/runtime"
receipt="$out/logs/${mode}_receipt.json"
trap 'status=$?; printf "{\"mode\":\"%s\",\"exit_code\":%d}\n" "$mode" "$status" > "$receipt"; echo "VERIFY_EXIT=$status"' EXIT
cd "$out/$tree"
unset QMAKESPEC QMAKEPATH QMAKEFEATURES QT_PLUGIN_PATH QML2_IMPORT_PATH
/usr/lib/qt5/bin/qmake "$project" -spec linux-g++ CONFIG+=release CONFIG-=debug 2>&1 | tee "$out/logs/${mode}_qmake.log"
make -j2 2>&1 | tee "$out/logs/${mode}_make.log"
if [[ "$mode" == build ]]; then exit 0; fi
export SN100_BATCH_TEST_ROOT="$out/runtime"
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}
"./$target" "$@" 2>&1 | tee "$out/logs/${mode}_run.log"
