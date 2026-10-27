#!/usr/bin/env bash
set -uo pipefail
work=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$work"
exec > >(tee smoke_host.log) 2>&1
remote=/home/rkshare/programs/sn100_shell/validation/20261027_01
host=lckfb@192.168.100.123
ssh -o BatchMode=yes -o ConnectTimeout=8 "$host" "test ! -e $remote && mkdir -p $remote" || exit $?
scp -B board_smoke.sh report_snapshot.json "$host:$remote/" || exit $?
ssh -T -o BatchMode=yes "$host" "bash $remote/board_smoke.sh > $remote/smoke.log 2>&1"
code=$?
printf '%s\n' "$code" > smoke.exit
printf 'BOARD_SMOKE_SSH_EXIT=%s\n' "$code"
ssh -T -o BatchMode=yes "$host" "cat $remote/smoke.log"
ssh -T -o BatchMode=yes "$host" "tar -czf - -C $remote -- smoke.log board_smoke.sh report_snapshot.json local_results_fresh.log local_results_fresh.exit local_results_reopen.log local_results_reopen.exit pdf.log html.log report.pdf report.html" > smoke_evidence.tar.gz
mkdir -p smoke_evidence
tar -xzf smoke_evidence.tar.gz -C smoke_evidence
exit "$code"
