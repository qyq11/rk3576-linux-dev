set -euo pipefail
export LC_ALL=C
test "$(uname -r)" = 6.1.99
base=/home/rkshare/programs/rt_validation
test ! -e "$base"
mkdir -p "$base"
cd "$base"
apt-get -o Acquire::Retries=1 -o Acquire::http::Timeout=25 download rt-tests=2.5-1
package=$(find . -maxdepth 1 -name 'rt-tests_*.deb' -print -quit)
test -n "$package"
dpkg-deb -x "$package" tools
sha256sum "$package" tools/usr/bin/cyclictest > tools.sha256
file tools/usr/bin/cyclictest
ldd tools/usr/bin/cyclictest
tools/usr/bin/cyclictest --help > cyclictest_help.txt 2>&1
cat tools.sha256
cat cyclictest_help.txt
