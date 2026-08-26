# PerfLens

A C++20 Linux performance regression tool.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Use `./build/perflens run --help` or `./build/perflens compare --help` for options.
