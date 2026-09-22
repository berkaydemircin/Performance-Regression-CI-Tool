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

### Results

Measured on Ubuntu 24.04 with an Intel Core i5-12500H using a Release build. Standalone results below use 30 measured runs per side and CPU pinning. Reported changes are the median across seeds 7, 19 and 41.

| Workload | Runtime change | Supporting performance counter |
| --- | ---: | --- |
| CPU work | **+243.0%** | CPU cycles **+245.3%** |
| Memory access | **+261.9%** | Cache misses **+516.2%** |
| Branch behavior | **+88.7%** | Branch misses **268 K -> 15.27 M** |
| Allocation | **+1166.7%** | Instructions **+1300.9%** |
| Lock contention | **+874.7%** | Voluntary context switches **4 -> 25.2 K** |

The service benchmark also detected a throughput drop from **121.9 K/s to 32.7 K/s (-73.2%)**, while p95 latency increased from **8.45 us to 32.39 us (+283.2%)**.

Identical CPU workloads differed by at most **0.034% in median runtime** across the three seeds. Individual run distributions had runtime coefficients of variation between **0.16% and 0.27%**.

A 10% runtime budget correctly rejected the CPU regression with exit code `1` after measuring a **+243.3%** regression.

#### Measurement overhead

On the ~9 ms CPU microbenchmark, process measurement added **9.0%** end to end overhead and hw counters added **9.3%**. The measured target runtime with counters remained within about **1%** of process only measurement. CPU sampling is intentionally opt in and has substantially higher fixed overhead on short running workloads.

Note that these are synthetic workloads intended to verify regression detection and diagnostics. They may not make general performance claims about the project.
