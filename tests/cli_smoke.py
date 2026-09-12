#!/usr/bin/env python3
import argparse
import json
import subprocess
import tempfile
from pathlib import Path

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, default=Path("build"))
    args = parser.parse_args()
    executable = str((args.build / "perflens").resolve())

    def check(arguments, expected):
        result = subprocess.run([executable, *arguments], capture_output=True, text=True, timeout=15)
        if result.returncode != expected:
            raise AssertionError(f"{arguments}: expected {expected}, got {result.returncode}\n{result.stdout}\n{result.stderr}")
        return result

    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "result.json"
        common = ["compare", "--no-counters", "--warmup", "0", "--repeat", "2", "--seed", "7"]
        check([*common, "--baseline", "/bin/true", "--candidate", "/bin/true", "--json", str(output)], 0)
        data = json.loads(output.read_text())
        assert len(data["baseline"]["runs"]) == len(data["candidate"]["runs"]) == 2
        check([*common, "--baseline", "/bin/sleep 0.02", "--candidate", "/bin/sleep 0.2",
               "--max-runtime-regression", "50"], 1)
        check([*common, "--baseline", "/bin/true", "--candidate", "/bin/true", "--max-p99-regression", "5"], 2)
        check(["run", "--no-counters", "--", "/bin/false"], 2)
        check(["run", "--no-counters", "--", "/definitely/missing"], 2)
        check(["run", "--no-counters", "--timeout", "20ms", "--", "/bin/sleep", "2"], 2)
        config = Path(directory) / "bad.json"
        config.write_text('{"thresholds":{"runtme_pct":5}}')
        check([*common, "--baseline", "/bin/true", "--candidate", "/bin/true", "--config", str(config)], 2)
        check(["run", "--no-counters", "--timeout", "9223372036854775808ns", "--", "/bin/true"], 2)
    print("8 CLI checks passed")

if __name__ == "__main__":
    main()
