#!/usr/bin/env python3
"""Aggregate public phase-labelled latency runs; never read runtime databases.

Usage: python3 analyze_latency.py /absolute/.../attempts/<attempt-id>
The 18 raw JSON results/logs remain unchanged. Failed/incomplete batches produce
no success summary. This helper does not build, run GUI, or sample a process.
"""

import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import re
import statistics
import sys


CONDITIONS = {
    "burst": ("burstNavigation", "连续100次换页", "同步测试驱动"),
    "capture_burst": ("burstNavigation", "截图后连续换页", "截图＋同步测试驱动"),
    "proc_burst": ("burstNavigation", "采样＋连续换页", "采样＋同步测试驱动"),
    "paced": ("pacedNavigation", "逐次换页让出事件循环", "每次换页后10ms事件循环"),
    "normal": ("normalOperations", "正常业务操作", "保留原弹窗与动画"),
    "overhead": ("evidenceOverhead", "截图与核验对照", "测试采证开销"),
}


def require(test, message):
    if not test:
        raise ValueError(message)


def number(value, name):
    require(isinstance(value, (int, float)) and not isinstance(value, bool), name + " must be numeric")
    value = float(value)
    require(math.isfinite(value) and value >= 0, name + " must be finite and nonnegative")
    return value


def read_json(path):
    require(path.is_file() and not path.is_symlink(), "Missing/nonregular public JSON: " + str(path))
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def resolve_input(value, attempt):
    require(isinstance(value, str) and value, "Manifest path must be a nonempty string")
    path = Path(value)
    if not path.is_absolute():
        path = attempt / path
    path = path.resolve()
    require(attempt == path or attempt in path.parents, "Input must stay inside the attempt: " + str(path))
    return path


def distribution(values):
    values = sorted(values)
    require(bool(values), "Cannot summarize an empty measurement set")
    return {
        "samples": len(values),
        "min_ms": values[0],
        "median_ms": statistics.median(values),
        "p95_ms": values[max(0, math.ceil(len(values) * 0.95) - 1)],
        "max_ms": values[-1],
    }


def approximately(a, b):
    return abs(a - b) <= max(0.00001, abs(a) * 0.000001)


def trace_metrics(trace, context):
    require(isinstance(trace, dict), context + ": missing trace")
    require(trace.get("timer_interval_ms") == 10, context + ": expected 10 ms timer")
    phases = trace.get("phases")
    intervals = trace.get("heartbeat_intervals")
    require(isinstance(phases, list) and phases, context + ": missing phases")
    require(isinstance(intervals, list) and intervals, context + ": missing delivered heartbeats")
    by_id = {}
    for phase in phases:
        ident = phase.get("id")
        require(isinstance(ident, int) and ident not in by_id, context + ": duplicate/invalid phase id")
        begin = number(phase.get("begin_ms"), context + ".phase.begin")
        end = number(phase.get("end_ms"), context + ".phase.end")
        duration = number(phase.get("duration_ms"), context + ".phase.duration")
        require(end >= begin and approximately(duration, end - begin), context + ": inconsistent phase time")
        require(isinstance(phase.get("name"), str) and isinstance(phase.get("category"), str), context + ": missing phase label")
        deliveries = phase.get("heartbeat_deliveries_during_phase")
        require(isinstance(deliveries, int) and deliveries >= 0, context + ": invalid phase heartbeat count")
        number(phase.get("process_cpu_ms"), context + ".phase.process_cpu_ms")
        by_id[ident] = phase
    prior_now = None
    phase_gaps = {ident: [] for ident in by_id}
    phase_overlaps = {ident: [] for ident in by_id}
    for interval in intervals:
        begin = number(interval.get("previous_ms"), context + ".heartbeat.previous")
        end = number(interval.get("now_ms"), context + ".heartbeat.now")
        gap = number(interval.get("gap_ms"), context + ".heartbeat.gap")
        require(end >= begin and approximately(gap, end - begin), context + ": inconsistent heartbeat interval")
        if prior_now is not None:
            require(approximately(begin, prior_now), context + ": heartbeat interval chain is discontinuous")
        prior_now = end
        listed = interval.get("overlapping_phases")
        require(isinstance(listed, list), context + ": missing phase overlaps")
        listed_by_id = {}
        for overlap in listed:
            ident = overlap.get("phase_id")
            require(ident in by_id and ident not in listed_by_id, context + ": duplicate/unknown phase overlap")
            listed_by_id[ident] = overlap
        expected = {}
        for ident, phase in by_id.items():
            overlap_begin = max(begin, phase["begin_ms"])
            overlap_end = min(end, phase["end_ms"])
            if overlap_end > overlap_begin:
                expected[ident] = (overlap_begin, overlap_end)
        require(set(listed_by_id) == set(expected), context + ": overlap membership differs from interval intersection")
        for ident, (overlap_begin, overlap_end) in expected.items():
            overlap = listed_by_id[ident]
            require(approximately(number(overlap.get("overlap_begin_ms"), "overlap begin"), overlap_begin)
                    and approximately(number(overlap.get("overlap_end_ms"), "overlap end"), overlap_end)
                    and approximately(number(overlap.get("overlap_duration_ms"), "overlap duration"), overlap_end - overlap_begin),
                    context + ": overlap times differ from interval intersection")
            require(overlap.get("name") == by_id[ident]["name"] and overlap.get("category") == by_id[ident]["category"],
                    context + ": inconsistent overlap labels")
            phase_gaps[ident].append(gap)
            phase_overlaps[ident].append(overlap_end - overlap_begin)
    values = [interval["gap_ms"] for interval in intervals]
    stats = distribution(values)
    previous_summary = trace.get("heartbeat", {})
    require(previous_summary.get("samples") == len(values)
            and approximately(previous_summary.get("max_ms", -1), stats["max_ms"])
            and approximately(previous_summary.get("p95_ms", -1), stats["p95_ms"]), context + ": trace summary disagrees with raw heartbeats")
    max_interval = max(intervals, key=lambda item: item["gap_ms"])
    tail = number(trace.get("undelivered_tail_ms"), context + ".undelivered_tail")
    require(approximately(trace.get("last_delivered_heartbeat_ms", -1), intervals[-1]["now_ms"]), context + ": final delivered heartbeat mismatch")
    require(approximately(trace.get("elapsed_ms", -1), intervals[-1]["now_ms"] + tail), context + ": final tail mismatch")
    phase_rows = []
    for phase in phases:
        ident = phase["id"]
        # Full gaps can overlap many short/nested phases. Keep overlap length
        # separate; never relabel a full long gap as each child phase's duration.
        phase_rows.append({**phase,
            "max_intersecting_full_heartbeat_gap_ms": max(phase_gaps[ident], default=0),
            "max_temporal_overlap_with_one_heartbeat_ms": max(phase_overlaps[ident], default=0),
            "intersecting_delivered_heartbeat_intervals": len(phase_gaps[ident])})
    return stats, max_interval, phase_rows


def summarize_phases(rows):
    groups = {}
    for row in rows:
        groups.setdefault((row["name"], row["category"]), []).append(row)
    output = []
    for (name, category), values in sorted(groups.items()):
        output.append({"name": name, "category": category,
            "duration": distribution([value["duration_ms"] for value in values]),
            "duration_sum_ms": sum(value["duration_ms"] for value in values),
            "process_cpu_ms": distribution([value["process_cpu_ms"] for value in values]),
            "heartbeat_deliveries_during_phase": [value["heartbeat_deliveries_during_phase"] for value in values],
            "max_intersecting_full_heartbeat_gap_ms": max(value["max_intersecting_full_heartbeat_gap_ms"] for value in values),
            "max_temporal_overlap_with_one_heartbeat_ms": max(value["max_temporal_overlap_with_one_heartbeat_ms"] for value in values)})
    return output


def validate_condition(condition, result, trace, phase_rows, context):
    require(result.get("business_assertions_passed") is True, context + ": business assertions did not pass")
    require(result.get("workers_closed_after_trace") is True, context + ": workers not confirmed closed after trace")
    require(trace.get("test_proc_sampler_enabled") is (condition == "proc_burst"), context + ": sampler flag is wrong")
    if condition in {"burst", "capture_burst", "proc_burst"}:
        require(result.get("navigation_clicks") == 100, context + ": expected 100 navigation clicks")
        require(result.get("pre_navigation_screenshot_enabled") is (condition == "capture_burst"), context + ": capture flag is wrong")
        bursts = [p for p in phase_rows if "100_synchronous_widget_clicks" in p["name"]]
        require(len(bursts) == 1, context + ": expected one exact burst span")
        return {"burst_duration_ms": bursts[0]["duration_ms"],
            "burst_heartbeat_deliveries": bursts[0]["heartbeat_deliveries_during_phase"],
            "largest_single_gap_overlap_fraction_of_burst": bursts[0]["max_temporal_overlap_with_one_heartbeat_ms"] / bursts[0]["duration_ms"] if bursts[0]["duration_ms"] else 0,
            "new_run_supports_synchronous_driver_starvation": bursts[0]["heartbeat_deliveries_during_phase"] == 0
                and bursts[0]["max_temporal_overlap_with_one_heartbeat_ms"] >= bursts[0]["duration_ms"] * .99}
    if condition == "paced":
        require(result.get("navigation_clicks") == 100 and result.get("event_loop_interval_after_each_click_ms") == 10,
                context + ": paced navigation setup differs")
    elif condition == "normal":
        require(result.get("successful_adds") == 10 and result.get("successful_deletes") == 10
                and result.get("successful_hospital_saves") == 10, context + ": incomplete normal workload")
        require(result.get("external_evidence_work_after_trace") is True, context + ": public verification boundary missing")
        require(all(p["category"] in {"normal_operation", "event_loop_interval"} for p in phase_rows),
                context + ": test evidence or sampler phase contaminates normal trace")
    elif condition == "overhead":
        require(result.get("screenshots_password_fields_empty") is True, context + ": screenshot privacy assertion missing")
        counts = {}
        for p in phase_rows:
            counts[p["name"]] = counts.get(p["name"], 0) + 1
        for name in ("evidence.window_grab", "evidence.pregrabbed_png_encode", "evidence.preencoded_file_write_flush",
                     "evidence.original_window_grab_save", "evidence.two_public_sql_snapshots", "evidence.proc_resource_sample"):
            require(counts.get(name) == 5, context + ": expected five repetitions of " + name)
        for name in ("evidence.clipboard_ascii_input", "evidence.keyboard_ascii_input"):
            require(counts.get(name) == 10, context + ": expected ten repetitions of " + name)
    return {}


def csv_text(rows, fields):
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=fields, extrasaction="ignore", lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("attempt_dir", type=Path)
    args = parser.parse_args()
    attempt = args.attempt_dir.resolve()
    require(attempt.is_dir() and "o03_2_latency" in attempt.parts, "Use the dedicated latency attempt directory")
    manifest_path = attempt / "runs_manifest.json"
    manifest = read_json(manifest_path)
    rows = manifest if isinstance(manifest, list) else manifest.get("rows", manifest.get("runs"))
    require(isinstance(rows, list) and len(rows) == 18, "Expected exactly 18 rows: six conditions x three fresh processes")
    conditions = {condition: [] for condition in CONDITIONS}
    run_summaries = []
    phase_csv = []
    heartbeat_csv = []
    timeline_candidates = []
    seen_pairs, seen_dirs, seen_pids = set(), set(), set()
    seen_builds, seen_manifests = set(), set()
    input_fingerprints = [{"path": "runs_manifest.json", "sha256": sha256(manifest_path)}]
    for row in rows:
        require(isinstance(row, dict), "Manifest row must be an object")
        condition, round_number = row.get("condition"), row.get("round")
        require(condition in CONDITIONS, "Unknown condition: " + str(condition))
        require(isinstance(round_number, int) and not isinstance(round_number, bool), "Round must be integer")
        pair = condition, round_number
        require(pair not in seen_pairs, "Duplicate condition/round: " + str(pair))
        seen_pairs.add(pair)
        require(row.get("slot") == CONDITIONS[condition][0], "Wrong test slot for " + condition)
        require(row.get("exit_code") == 0, "Failed process in " + str(pair))
        number(row.get("elapsed_seconds"), "process elapsed_seconds")
        evidence = resolve_input(row.get("evidence_dir"), attempt)
        require(evidence.is_dir() and evidence not in seen_dirs, "Each process requires its own evidence directory")
        seen_dirs.add(evidence)
        result_path = resolve_input(row.get("result_path"), attempt)
        log_path = resolve_input(row.get("log_path"), attempt)
        require(evidence == result_path.parent or evidence in result_path.parents, "Result must belong to its run evidence directory")
        result = read_json(result_path)
        require(log_path.is_file() and not log_path.is_symlink(), "Missing public test log")
        totals = re.findall(r"Totals:\s*(\d+)\s+passed,\s*(\d+)\s+failed,\s*(\d+)\s+skipped", log_path.read_text(encoding="utf-8", errors="replace"))
        require(totals and all(int(failed) == 0 and int(skipped) == 0 for _, failed, skipped in totals), "Log does not confirm unskipped passing QtTest: " + str(log_path))
        pid = result.get("process_id")
        require(isinstance(pid, (int, float)) and pid > 0 and int(pid) == pid and pid not in seen_pids, "Expected a distinct valid process ID for every run")
        seen_pids.add(pid)
        context = condition + "/round" + str(round_number)
        trace = result.get("trace")
        heartbeat, max_gap, phase_rows = trace_metrics(trace, context)
        mechanism = validate_condition(condition, result, trace, phase_rows, context)
        metadata_path = evidence / "run_buildmetadata.json"
        metadata = read_json(metadata_path)
        require(metadata.get("status") == "success", context + ": no successful frozen build metadata")
        for key, values in (("binary_sha256", seen_builds), ("source_manifest_sha256", seen_manifests)):
            digest = metadata.get(key)
            require(isinstance(digest, str) and re.fullmatch(r"[a-fA-F0-9]{64}", digest), context + ": invalid build fingerprint")
            values.add(digest.lower())
        require(result.get("platform") == "xcb", context + ": expected real XCB test")
        maximum = {key: max_gap[key] for key in ("previous_ms", "now_ms", "gap_ms", "overlapping_phases")}
        summary = {"condition": condition, "round": round_number, "slot": row["slot"], "process_id": pid,
            "result_path": result_path.relative_to(attempt).as_posix(), "log_path": log_path.relative_to(attempt).as_posix(),
            "heartbeat": heartbeat, "heartbeat_intervals_above_100ms": sum(i["gap_ms"] > 100 for i in trace["heartbeat_intervals"]),
            "heartbeat_intervals_above_500ms": sum(i["gap_ms"] > 500 for i in trace["heartbeat_intervals"]),
            "maximum_heartbeat_interval": maximum, "undelivered_tail_ms": trace["undelivered_tail_ms"],
            "trace_elapsed_ms": trace["elapsed_ms"], "process_cpu_seconds": trace["process_cpu_seconds"],
            "cpu_percent_one_core": trace["cpu_percent_one_core"], "process_elapsed_seconds": row["elapsed_seconds"],
            "business_assertions_passed": True, "workers_closed_after_trace": True,
            "phase_summaries": summarize_phases(phase_rows), "mechanism_observation": mechanism}
        run_summaries.append(summary)
        conditions[condition].append((summary, phase_rows))
        for item in summary["phase_summaries"]:
            phase_csv.append({"condition": condition, "round": round_number, "process_id": pid,
                "name": item["name"], "category": item["category"], "phase_count": item["duration"]["samples"],
                "duration_min_ms": item["duration"]["min_ms"], "duration_median_ms": item["duration"]["median_ms"],
                "duration_p95_ms": item["duration"]["p95_ms"], "duration_max_ms": item["duration"]["max_ms"],
                "duration_sum_ms": item["duration_sum_ms"], "process_cpu_median_ms": item["process_cpu_ms"]["median_ms"],
                "heartbeat_deliveries_during_phase": json.dumps(item["heartbeat_deliveries_during_phase"]),
                "max_intersecting_full_heartbeat_gap_ms": item["max_intersecting_full_heartbeat_gap_ms"],
                "max_temporal_overlap_with_one_heartbeat_ms": item["max_temporal_overlap_with_one_heartbeat_ms"]})
        heartbeat_csv.append({"condition": condition, "round": round_number, "process_id": pid,
            "samples": heartbeat["samples"], "p95_ms": heartbeat["p95_ms"], "max_ms": heartbeat["max_ms"],
            "above_100ms": summary["heartbeat_intervals_above_100ms"], "above_500ms": summary["heartbeat_intervals_above_500ms"],
            "max_gap_begin_ms": maximum["previous_ms"], "max_gap_end_ms": maximum["now_ms"],
            "undelivered_tail_ms": trace["undelivered_tail_ms"], "trace_elapsed_ms": trace["elapsed_ms"],
            "cpu_percent_one_core": trace["cpu_percent_one_core"],
            "max_gap_overlap_names": json.dumps(list(dict.fromkeys(o["name"] for o in maximum["overlapping_phases"])), ensure_ascii=False)})
        for path in (result_path, log_path, metadata_path):
            input_fingerprints.append({"path": path.relative_to(attempt).as_posix(), "sha256": sha256(path)})
        if condition in {"burst", "capture_burst"}:
            timeline_candidates.append((heartbeat["max_ms"], summary, trace))
    require(len(seen_builds) == 1 and len(seen_manifests) == 1, "All 18 runs must use exactly the same frozen binary and source manifest")
    round_sets = []
    condition_summaries, chart_groups = [], []
    for condition, (_, label, kind) in CONDITIONS.items():
        records = sorted(conditions[condition], key=lambda item: item[0]["round"])
        require(len(records) == 3, "Expected three processes for " + condition)
        round_sets.append({record[0]["round"] for record in records})
        maxima = [record[0]["heartbeat"]["max_ms"] for record in records]
        condition_summaries.append({"condition": condition, "processes": 3,
            "rounds": [record[0]["round"] for record in records],
            "run_max_heartbeat_ms": maxima, "run_maximum_range": {"min_ms": min(maxima), "median_ms": statistics.median(maxima), "max_ms": max(maxima)},
            "run_p95_heartbeat_ms": [record[0]["heartbeat"]["p95_ms"] for record in records],
            "phase_summaries": summarize_phases([phase for _, phase_rows in records for phase in phase_rows]),
            "new_run_driver_starvation_observations": [record[0]["mechanism_observation"] for record in records] if condition in {"burst", "capture_burst", "proc_burst"} else []})
        chart_groups.append({"condition": condition, "label": label, "kind": kind, "values_ms": maxima})
    require(all(rounds == round_sets[0] for rounds in round_sets), "Condition round sets differ")
    _, timeline_run, timeline_trace = max(timeline_candidates, key=lambda item: item[0])
    chart_phases = []
    for phase in timeline_trace["phases"]:
        if "100_synchronous_widget_clicks" in phase["name"]:
            name = "连续100次同步换页"
        elif phase["category"] == "evidence_capture":
            name = "换页前全窗口截图及PNG保存"
        else:
            continue
        chart_phases.append({"begin_ms": phase["begin_ms"], "end_ms": phase["end_ms"], "name": name, "original_name": phase["name"]})
    chart_data = {"groups": chart_groups,
        "timeline": {"title": "新运行的连续换页与心跳时间轴", "condition": timeline_run["condition"],
            "round": timeline_run["round"], "process_id": timeline_run["process_id"], "source_result": timeline_run["result_path"],
            "heartbeats": [{"now_ms": beat["now_ms"], "gap_ms": beat["gap_ms"]} for beat in timeline_trace["heartbeat_intervals"]],
            "phases": chart_phases},
        "notes": ["每柱为一独立进程的实际最大GUI心跳处理间隔；各条件分别统计，不合并成产品指标。", "原1.556秒观测无阶段标记，不能追认其确切实例原因。", "500ms仅候选观察线；WSL短测不代表RK、像素反馈、仪器或长稳通过。"]}
    all_starvation = all(run[0]["mechanism_observation"].get("new_run_supports_synchronous_driver_starvation") is True
                         for condition in ("burst", "capture_burst", "proc_burst") for run in conditions[condition])
    summary = {"status": "complete_batch_validated", "schema_version": 1, "runs": 18, "processes": 18,
        "conditions": condition_summaries, "run_summaries": sorted(run_summaries, key=lambda item: (list(CONDITIONS).index(item["condition"]), item["round"])),
        "source_manifest_sha256": next(iter(seen_manifests)), "binary_sha256": next(iter(seen_builds)),
        "input_fingerprints": input_fingerprints,
        "observations": {"all_nine_burst_runs_show_zero_deliveries_and_one_gap_covering_burst": all_starvation,
            "historical_peak_ms": 1556.009424, "historical_exact_instance_cause_proven": False,
            "interpretation": "New labelled runs can demonstrate synchronous test-driver starvation when burst phases deliver no timer callbacks and a following interval covers them. This is a reproduced mechanism, not retrospective proof for the original unlabelled 1556.009424ms instance."},
        "measurement_limits": [
            "Heartbeats are GUI timer delivery intervals, not input-to-visible/compositor latency or worker SQL completion.",
            "Normal full-operation durations include keyboard injection, worker completion and original modal animations; long duration with delivered heartbeats is not equal-duration GUI blocking.",
            "Temporal overlap permits nested phases; overlapping durations must not be summed as exclusive time or treated as call-stack proof.",
            "PNG-to-memory includes QPixmap conversion/allocation and encoding; file flush/close excludes fsync/durable storage assurance.",
            "Per-run heartbeat p95 uses nearest rank ceil(0.95*N)-1; three run maxima are shown individually and by min/median/max, without a misleading three-sample p99.",
            "Normal workload, test driver bursts and evidence overhead remain separate conditions; no combined product responsiveness distribution is produced.",
            "Source is the current frozen formal program with declared O03-3 dependencies; visual/animation policy is retained, but historical O03-2 source identity is not implied.",
            "Whole-process CPU includes GUI, SQL workers and test work. WSL measurements do not accept RK3576, physical input, instrument, same-board control or long stability."
        ]}
    outputs = {
        "analysis_summary.json": json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        "phase_summary.csv": csv_text(phase_csv, ["condition", "round", "process_id", "name", "category", "phase_count", "duration_min_ms", "duration_median_ms", "duration_p95_ms", "duration_max_ms", "duration_sum_ms", "process_cpu_median_ms", "heartbeat_deliveries_during_phase", "max_intersecting_full_heartbeat_gap_ms", "max_temporal_overlap_with_one_heartbeat_ms"]),
        "heartbeat_summary.csv": csv_text(heartbeat_csv, ["condition", "round", "process_id", "samples", "p95_ms", "max_ms", "above_100ms", "above_500ms", "max_gap_begin_ms", "max_gap_end_ms", "undelivered_tail_ms", "trace_elapsed_ms", "cpu_percent_one_core", "max_gap_overlap_names"]),
        "chart_data.json": json.dumps(chart_data, ensure_ascii=False, indent=2) + "\n",
    }
    for name, payload in outputs.items():
        destination = attempt / name
        require(not destination.is_symlink(), "Refuse symlinked generated analysis output")
        temporary = attempt / (name + ".new")
        with temporary.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
        temporary.replace(destination)
    print("Validated 18 passing fresh-process runs; wrote separate heartbeat/phase summaries and chart data.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print("Latency aggregation refused: " + str(error), file=sys.stderr)
        sys.exit(2)
