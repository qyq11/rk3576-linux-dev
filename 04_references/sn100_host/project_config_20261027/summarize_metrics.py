from pathlib import Path
import csv
import json
import math
import re

here = Path(__file__).resolve().parent

def summarize(directory, period):
    rows = list(csv.DictReader((directory / "resources.tsv").open(encoding="utf-8"), delimiter="\t"))
    valid = [r for r in rows if all(str(r.get(k, "")).replace(".", "", 1).isdigit()
             for k in ("uptime_s", "utime_ticks", "stime_ticks", "rss_kib", "pss_kib"))]
    duration = float(valid[-1]["uptime_s"]) - float(valid[0]["uptime_s"])
    ticks = sum(int(valid[-1][key]) - int(valid[0][key]) for key in ("utime_ticks", "stime_ticks"))
    result = {"samples": len(valid), "nominalSamplingSleepSeconds": period,
        "meanActualSamplingIntervalSeconds": round(duration / (len(valid) - 1), 3), "sampledDurationSeconds": round(duration, 3),
        "clockTicksPerSecond": 100, "cpuPercentOneCoreOverSampledSpan": round(ticks / 100 / duration * 100, 2),
        "peakRssKiB": max(int(r["rss_kib"]) for r in valid),
        "peakPssKiB": max(int(r["pss_kib"]) for r in valid),
        "peakThreads": max(int(r["threads"]) for r in valid),
        "peakFdCount": max(int(r["fd_count"]) for r in valid)}
    log = (directory / "app.log").read_text(encoding="utf-8", errors="replace")
    times = sorted(int(v) for v in re.findall(r"SAVE_COMPLETE_MS (\d+)", log))
    if times:
        result["saveCommitAndReloadMs"] = {"count": len(times), "min": times[0], "max": times[-1],
            "p95NearestRank": times[math.ceil(len(times) * .95) - 1], "samples": times,
            "scope": "鼠标保存事件到提交并重新读回；不等于屏幕可见反馈延迟"}
    result["scope"] = "RK短时基线，含启动和驱动操作/截图；门槛未确认，不代表长期或同板控制放行"
    (directory / "resource_summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return result

result = {"page": summarize(here / "page_rk_09", .2)}
for stage in ("old_anonymous", "new_migration", "new_reopen"):
    result[stage] = summarize(here / "full_shell_rk" / stage, 1)
(here / "metrics_summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps(result, ensure_ascii=False, indent=2))
