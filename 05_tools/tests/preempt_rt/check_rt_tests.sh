set -euo pipefail
export LC_ALL=C
cd /home/rkshare/programs/rt_validation
ldd tools/usr/bin/cyclictest
set +e
tools/usr/bin/cyclictest --help > cyclictest_help.txt 2>&1
help_exit=$?
set -e
printf 'HELP_EXIT=%s\n' "$help_exit"
cat tools.sha256
cat cyclictest_help.txt
