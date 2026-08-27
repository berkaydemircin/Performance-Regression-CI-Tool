# Workload metrics

PerfLens sets `PERFLENS_METRICS_FILE` for each run. Write your results to that path before the program exits.

```json
{
  "operations": 1000000,
  "throughput": 142131.2,
  "latency": {
    "p50_us": 481,
    "p95_us": 1031,
    "p99_us": 1822
  }
}
```

Throughput is in operations per second and latency is in microseconds. You can omit fields, but the file needs at least one measurement.

Operation counts must be positive integers. Other values must be finite and nonnegative, and latency percentiles must be in order. Invalid data makes the run fail.
