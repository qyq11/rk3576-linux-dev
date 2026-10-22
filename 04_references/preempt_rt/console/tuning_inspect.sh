#!/usr/bin/env bash
set -Eeuo pipefail
export LC_ALL=C
printf '[kernel_and_rt]\n'
uname -a
cat /sys/kernel/realtime
cat /proc/cmdline
printf '[cpu_topology_and_frequency]\n'
lscpu | grep -E 'Architecture:|CPU\(s\):|On-line CPU|Model name:|CPU max MHz:|CPU min MHz:|NUMA node'
for policy in /sys/devices/system/cpu/cpufreq/policy*; do
    printf '%s\n' "$policy"
    for field in related_cpus scaling_driver scaling_governor scaling_available_governors cpuinfo_min_freq cpuinfo_max_freq scaling_min_freq scaling_max_freq scaling_cur_freq; do
        if [[ -r $policy/$field ]]; then printf '%s=' "$field"; cat "$policy/$field"; fi
    done
done
for path in /sys/devices/system/cpu/cpu[0-9]*/cpu_capacity; do
    if [[ -r $path ]]; then printf '%s=' "$path"; cat "$path"; fi
done
printf '[cpu_isolation]\n'
cat /sys/devices/system/cpu/online
if [[ -r /sys/devices/system/cpu/isolated ]]; then cat /sys/devices/system/cpu/isolated; fi
printf '[temperature_snapshot_millicelsius]\n'
for zone in /sys/class/thermal/thermal_zone*; do
    printf '%s ' "$zone"
    cat "$zone/type" "$zone/temp"
done
printf '[active_measurements_and_priorities]\n'
ps -eLo pid,tid,cls,rtprio,pri,psr,comm | awk 'NR==1 || $7 ~ /^(cyclictest|hackbench|stress-ng|ktimersd)/ {print; next} $7 ~ /^irq\// {if (irq_count++ < 20) print}'
printf '[interrupt_counts]\n'
grep -E 'CPU0|arch_timer|eth|gmac|dwmac|xhci|usb|mmc' /proc/interrupts || true
printf '[tracing_support]\n'
for command_name in trace-cmd rtla; do command -v "$command_name" || true; done
sudo -n bash -s <<'ROOT'
set -eu
for path in /sys/kernel/tracing/available_tracers /sys/kernel/debug/tracing/available_tracers; do
    if [ -r "$path" ]; then printf '%s\n' "$path"; cat "$path"; fi
done
ROOT
printf '[selected_config]\n'
zcat /proc/config.gz | grep -E '^(CONFIG_(PREEMPT_RT|PREEMPT_LAZY|HIGH_RES_TIMERS|HZ=|NO_HZ|RCU_NOCB_CPU|OSNOISE_TRACER|TIMERLAT_TRACER|IRQSOFF_TRACER|PREEMPT_TRACER|FTRACE|FUNCTION_TRACER|SCHED_TRACER|IRQ_FORCED_THREADING))|^# CONFIG_(OSNOISE_TRACER|TIMERLAT_TRACER|IRQSOFF_TRACER|PREEMPT_TRACER|FUNCTION_TRACER)'
printf '[READ_ONLY_INSPECTION]=COMPLETE\n'
