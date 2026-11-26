"""Summarize matched playback trials recorded as JSON lines, one object per trial.

Group fields: device, network, app, version, source, cache (cold or warm).
Required outcome: success (boolean). Optional numeric measurements use the
units in METRICS. Use null for unavailable measurements. Record first_frame_ms
from source selection to visible video, including torrent resolution.
"""
import argparse
import json
import math
import statistics
from collections import defaultdict


GROUP = ("device", "network", "app", "version", "source", "cache")
METRICS = ("first_frame_ms", "seek_ms", "rebuffers_per_min", "cpu_percent",
           "peak_ram_mib", "throughput_mbps")


def summarize(rows):
    groups = defaultdict(list)
    for row in rows:
        key = tuple(row[field] for field in GROUP)
        if not all(isinstance(v, str) and v.strip() for v in key):
            raise ValueError("each group field must be a nonempty string")
        if row["cache"] not in ("cold", "warm") or type(row["success"]) is not bool:
            raise ValueError("invalid cache or success value")
        for metric in METRICS:
            v = row.get(metric)
            if v is not None and (type(v) not in (int, float) or not math.isfinite(v) or v < 0):
                raise ValueError(f"invalid {metric}")
        groups[key].append(row)
    for key, trials in sorted(groups.items()):
        failed = sum(not trial["success"] for trial in trials)
        report = dict(zip(GROUP, key), trials=len(trials), failures=failed,
                      failure_rate=failed / len(trials))
        for metric in METRICS:
            values = sorted(t[metric] for t in trials if t.get(metric) is not None)
            if values:
                report[metric] = dict(n=len(values), median=statistics.median(values),
                                      p95=values[math.ceil(.95 * len(values)) - 1])
        yield report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trials", type=argparse.FileType("r", encoding="utf-8"))
    args = parser.parse_args()
    for result in summarize(json.loads(line) for line in args.trials if line.strip()):
        print(json.dumps(result))
