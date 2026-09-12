#!/usr/bin/env python3

import argparse
import json
import os
import random
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

def describe(values):
    mean = statistics.fmean(values)
    return {
        "median_ns": statistics.median(values),
        "mean_ns": mean,
        "cv_pct": statistics.pstdev(values) / mean * 100 if mean else 0.0,
    }

def main():
    parser = argparse.ArgumentParser(description="Measure end-to-end runner cost and target runtime separately")
    parser.add_argument("--perflens", type=Path, default=Path("build/perflens"))
    parser.add_argument("--repeat", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--profile-frequency", type=int, default=0)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command and args.command[0] == "--" else args.command
    if not command or args.repeat < 2 or args.warmup < 0 or args.profile_frequency < 0:
        parser.error("provide a command, repeat >= 2, warmup >= 0 and profile-frequency >= 0")
    if args.cpu is not None and args.cpu not in os.sched_getaffinity(0):
        parser.error("CPU is outside the allowed affinity mask")

    modes = {"raw": [], "process": ["--no-counters"], "counters": []}
    if args.profile_frequency:
        modes["sampling"] = ["--no-counters", "--profile-frequency", str(args.profile_frequency)]
    samples = {mode: [] for mode in modes}

    def run(mode):
        with tempfile.NamedTemporaryFile(suffix=".json") as output:
            if mode == "raw":
                invocation = command if args.cpu is None else ["taskset", "-c", str(args.cpu), *command]
            else:
                invocation = [str(args.perflens), "run", "--json", output.name, *modes[mode]]
                if args.cpu is not None:
                    invocation += ["--cpu", str(args.cpu)]
                invocation += ["--", *command]
            started = time.perf_counter_ns()
            subprocess.run(invocation, check=True, stdout=subprocess.DEVNULL)
            elapsed = time.perf_counter_ns() - started
            sample = {"elapsed_ns": elapsed, "available": True, "reason": ""}
            if mode != "raw":
                result = json.load(output)["runs"][0]
                sample["target_ns"] = result["process"]["wall_time_ns"]
                if mode in ("counters", "sampling"):
                    diagnostic = result["counters" if mode == "counters" else "cpu_profile"]
                    sample["available"] = diagnostic["available"]
                    sample["reason"] = diagnostic["unavailable_reason"]
                    if mode == "sampling":
                        sample["sample_count"] = sum(f["samples"] for f in diagnostic["functions"])
                        if sample["sample_count"] == 0:
                            sample["available"] = False
                            sample["reason"] = sample["reason"] or "no samples collected"
            return sample

    generator = random.Random(args.seed)
    for _ in range(args.warmup):
        order = list(modes)
        generator.shuffle(order)
        for mode in order:
            run(mode)
    order = list(modes) * args.repeat
    generator.shuffle(order)
    for mode in order:
        samples[mode].append(run(mode))

    raw_median = describe([s["elapsed_ns"] for s in samples["raw"]])["median_ns"]
    process_median = describe([s["target_ns"] for s in samples["process"]])["median_ns"]
    results = {}
    for mode, values in samples.items():
        available = all(s["available"] for s in values)
        elapsed = describe([s["elapsed_ns"] for s in values])
        results[mode] = {
            "available": available,
            "end_to_end": elapsed,
            "end_to_end_change_pct": (elapsed["median_ns"] / raw_median - 1) * 100 if available else None,
            "samples": values,
        }
        if mode != "raw":
            target = describe([s["target_ns"] for s in values])
            results[mode]["target"] = target
            results[mode]["target_change_vs_process_pct"] = (target["median_ns"] / process_median - 1) * 100 if available else None
    print(json.dumps({"seed": args.seed, "cpu": args.cpu, "command": command,
                      "order": order, "modes": results}, indent=2))

if __name__ == "__main__":
    main()
