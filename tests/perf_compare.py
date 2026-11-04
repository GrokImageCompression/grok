#!/usr/bin/env python3
import json
import os
import statistics
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

RUNS = 5
RSS_TOLERANCE = 1.10
RSS_SLACK_MB = 5
READ_TOLERANCE = 1.10
READ_SLACK = 16
TIME_TOLERANCE = 1.10
HTTP_PORT = 28731


def run_bench(bench, library, image, source, checksum=False):
    command = [bench, "decode", image, source] + (["checksum"] if checksum else [])
    environment = dict(os.environ, LD_PRELOAD=library)
    environment.pop("GRK_MERCURY", None)
    return json.loads(subprocess.run(command, env=environment, check=True, capture_output=True, text=True).stdout)


def http_stats():
    with urllib.request.urlopen(f"http://127.0.0.1:{HTTP_PORT}/stats") as response:
        return json.load(response)


def measure(bench, libraries, image, workflow):
    source = "callback" if workflow == "callback" else "file"
    target = f"http://127.0.0.1:{HTTP_PORT}/image" if workflow == "http" else image
    samples = {"base": [], "head": []}
    checksums = {tag: run_bench(bench, library, target, source, checksum=True)["checksum"]
                 for tag, library in libraries.items()}
    for run in range(RUNS):
        tags = ("base", "head") if run % 2 == 0 else ("head", "base")
        for tag in tags:
            if workflow == "http":
                http_stats()
            sample = run_bench(bench, libraries[tag], target, source)
            if workflow == "http":
                stats = http_stats()
                sample["read_calls"] = stats["requests"]
                sample["read_bytes"] = stats["bytes"]
            samples[tag].append(sample)
    summary = {}
    for tag, runs in samples.items():
        summary[tag] = {
            "ms": statistics.median(r["ms"] for r in runs),
            "rss_mb": max(r["rss_mb"] for r in runs),
            "read_calls": max(r["read_calls"] for r in runs),
            "read_bytes": max(r["read_bytes"] for r in runs),
        }
    summary["checksums_match"] = checksums["base"] == checksums["head"]
    return summary


def read_growth(result):
    base, head = result["base"], result["head"]
    return [f"{field} {base[field]} to {head[field]}" for field in ("read_calls", "read_bytes")
            if head[field] > base[field] * READ_TOLERANCE + READ_SLACK]


def regressions(result, workflow):
    base, head = result["base"], result["head"]
    found = []
    if not result["checksums_match"]:
        found.append("decoded samples differ")
    if head["rss_mb"] > base["rss_mb"] * RSS_TOLERANCE + RSS_SLACK_MB:
        found.append(f"peak RSS {base['rss_mb']:.0f} to {head['rss_mb']:.0f} MB")
    # each read is a request over http, and a cheap system call on local input
    if workflow == "http":
        found += read_growth(result)
    return found


def warnings(result, workflow):
    found = []
    if result["head"]["ms"] / result["base"]["ms"] > TIME_TOLERANCE:
        found.append("time")
    if workflow != "http":
        found += read_growth(result)
    return found


def main():
    bench, base_library, head_library = sys.argv[1:4]
    libraries = {"base": base_library, "head": head_library}
    lines = ["| image | workflow | base ms | head ms | ratio | base MB | head MB | base reads | head reads | status |",
             "|---|---|---:|---:|---:|---:|---:|---:|---:|---|"]
    failed = False
    for image in sys.argv[4:]:
        server = subprocess.Popen([sys.executable, "-I", str(Path(__file__).parent / "range_server.py"),
                                   image, str(HTTP_PORT)])
        # the server needs a moment to bind before the first request
        time.sleep(1)
        try:
            for workflow in ("file", "callback", "http"):
                result = measure(bench, libraries, image, workflow)
                base, head = result["base"], result["head"]
                ratio = head["ms"] / base["ms"]
                found = regressions(result, workflow)
                failed = failed or bool(found)
                warned = warnings(result, workflow)
                status = "; ".join(found) or ("warning: " + "; ".join(warned) if warned else "ok")
                lines.append(f"| {Path(image).name} | {workflow} | {base['ms']:.0f} | {head['ms']:.0f} | {ratio:.2f} "
                             f"| {base['rss_mb']:.0f} | {head['rss_mb']:.0f} | {base['read_calls']} "
                             f"| {head['read_calls']} | {status} |")
                print(lines[-1], flush=True)
        finally:
            server.kill()
            server.wait()
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a") as summary:
            summary.write("\n".join(lines) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
