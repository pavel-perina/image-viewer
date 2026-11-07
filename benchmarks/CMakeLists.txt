add_executable(iv_benchmarks
    halve_benchmark.cpp
)
target_link_libraries(iv_benchmarks PRIVATE iv_core benchmark::benchmark benchmark::benchmark_main)
