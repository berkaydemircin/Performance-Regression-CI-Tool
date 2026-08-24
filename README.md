# PerfLens

A C++20 Linux performance regression tool.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Run a program with `./build/perflens ./build/cpu_work`.
