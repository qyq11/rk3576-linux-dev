base=/home/rkshare/programs/sn100_shell
run=$base/runs/20261104_193133_resume02_107316
systemctl show sn100-resume-20261104-02.service -p LoadState -p ActiveState -p SubState -p Result -p ExecMainStatus || true
journalctl -u sn100-resume-20261104-02.service --no-pager -n 28
ls -la "$run"
for name in supervisor.log startup_failure.txt app.log exit_code result.txt; do
 if [[ -f $run/$name ]]; then printf '\nFILE=%s\n' "$name"; tail -n 20 "$run/$name"; fi
done
