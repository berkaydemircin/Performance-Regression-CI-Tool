#!/usr/bin/env python3

import argparse
import json
import random
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def raw_once(command: list[str]) -> float:
    started = time.perf_counter_ns()
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    return float(time.perf_counter_ns() - started)


def perflens_once(perflens: Path, command: list[str], extra: list[str]) -> tuple[float, bool, str]:
    with tempfile.NamedTemporaryFile(suffix=".json") as output:
        subprocess.run(
            [str(perflens), "run", "--json", output.name, *extra, "--", *command],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        run = json.load(output)["runs"][0]
    return (
        float(run["process"]["wall_time_ns"]),
        bool(run["counters"]["available"]),
        str(run["counters"]["unavailable_reason"]),
    )


def describe(values: list[float]) -> dict[str, float]:
    mean = statistics.fmean(values)
    return {
        "median_ns": statistics.median(values),
        "mean_ns": mean,
        "cv_pct": statistics.pstdev(values) / mean * 100 if mean else 0.0,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--perflens", type=Path, default=Path("build/perflens"))
    parser.add_argument("--repeat", type=int, default=10)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command and args.command[0] == "--" else args.command
    if not command:
        parser.error("a target command is required")

    raw_once(command)
    perflens_once(args.perflens, command, ["--no-counters"])
    perflens_once(args.perflens, command, [])

    modes = [mode for _ in range(args.repeat) for mode in ("raw", "process", "counters")]
    random.Random(args.seed).shuffle(modes)
    values: dict[str, list[float]] = {"raw": [], "process": [], "counters": []}
    counters_available = True
    counter_reason = ""
    for mode in modes:
        if mode == "raw":
            values[mode].append(raw_once(command))
        elif mode == "process":
            runtime, _, _ = perflens_once(args.perflens, command, ["--no-counters"])
            values[mode].append(runtime)
        else:
            runtime, available, reason = perflens_once(args.perflens, command, [])
            values[mode].append(runtime)
            counters_available = counters_available and available
            if not available:
                counter_reason = reason

    raw_description = describe(values["raw"])
    process_description = describe(values["process"])
    counter_description = describe(values["counters"])
    result = {
        "seed": args.seed,
        "target_alone": raw_description,
        "process_metrics": {
            **process_description,
            "runtime_change_pct": (process_description["median_ns"] / raw_description["median_ns"] - 1) * 100,
        },
        "hardware_counters": {
            **counter_description,
            "runtime_change_pct": (counter_description["median_ns"] / raw_description["median_ns"] - 1) * 100,
            "available": counters_available,
            "unavailable_reason": counter_reason,
        },
    }
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
