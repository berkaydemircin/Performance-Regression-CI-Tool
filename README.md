# PerfLens

A C++20 tool for checking whether a code change made a program slower. It compares a baseline and candidate and fails CI if the change exceeds the limits you set.

It supports repeated runs, warmups, CPU pinning and shuffled run order to ensure fairness.

## Build and test

Requires Linux, a C++20 compiler, CMake 3.20+, Git, binutils and Python3. CMake downloads the dependencies on the first build.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 tests/cli_smoke.py --build build
```

## Usage

Run a single program:

```bash
./build/perflens run --warmup 2 --repeat 10 --no-counters \
    --json run.json -- ./build/cpu_hotspot baseline
```

Compare two versions:

```bash
./build/perflens compare \
    --baseline "./build/cpu_hotspot baseline" \
    --candidate "./build/cpu_hotspot candidate" \
    --warmup 2 --repeat 10 --seed 7 --no-counters \
    --max-runtime-regression 10 --json comparison.json
```

The candidate in this example does extra CPU work. For your own project, use the old and new executable paths instead. Each command should start one program. If you need shell setup, put it in a script that ends with `exec`.

The example above allows up to a 10% increase in median runtime. The exit code is `0` for a pass, `1` if a limit is exceeded and `2` for an error.

You can also set `--max-p99-regression`, `--max-throughput-regression`, `--max-cpu-regression` and `--max-rss-regression`. These take percentages too. Throughput checks for a decrease, and the CPU limit uses time per operation. Latency, throughput and operation counts come from the [workload](docs/workload-protocol.md). A limit with missing data returns an error.

Add `--cpu N` to pin the program or `--timeout 5s` to limit each run. Remove `--no-counters` for hardware counters and add `--profile-frequency 99` for sampling. Sampling currently covers the main thread only. Both need perf access on the host; `--require-perf` makes unavailable collection fail the run.

## Services

```bash
./build/perflens compare \
    --baseline "./build/example_server --port 19090 --mode baseline" \
    --candidate "./build/example_server --port 19090 --mode candidate" \
    --workload "./build/example_workload --port 19090 --operations 2000" \
    --ready-tcp 127.0.0.1:19090 --ready-timeout 5s --timeout 10s \
    --warmup 1 --repeat 5 --no-counters --json service.json
```

Note that the port number can change. PerfLens starts a server and waits for it to accept connections, runs the client and stops the server afterwards. The client writes its results to `PERFLENS_METRICS_FILE`.

## Benchmarks

The examples cover CPU work, memory access, branches, allocation and lock contention. To measure PerfLens overhead:

```bash
python3 benchmarks/measure_overhead.py \
    --perflens build/perflens --warmup 3 --repeat 30 \
    --profile-frequency 99 \
    -- ./build/cpu_hotspot candidate > overhead.json
```

This reports total command time and target runtime separately. Results from unavailable collectors are left as null. Run on a quiet machine and save the CPU, compiler and build settings with the results.

I will but benchmark results here once I have the numbers.
