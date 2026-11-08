#pragma once

#include <algorithm>
#include <cstdint>
#include <exception>
#include <thread>
#include <vector>

namespace iv {

// Runs band(firstRow, endRow) over bands of rows on up to 8 threads, enough pixels to each to be
// worth a thread, and rethrows the first exception any of them threw.
template <typename Band>
void inBands(int rows, int width, const Band& band) {
    constexpr std::uint64_t kPixelsPerThread = 256 * 1024;
    const std::uint64_t pixels = static_cast<std::uint64_t>(std::max(rows, 0)) * static_cast<std::uint64_t>(width);
    const auto wanted = static_cast<int>(std::min<std::uint64_t>(pixels / kPixelsPerThread, 8));
    const int bands = std::clamp(std::min(wanted, static_cast<int>(std::thread::hardware_concurrency())), 1, std::max(rows, 1));
    std::vector<std::exception_ptr> errors(static_cast<std::size_t>(bands));
    {
        std::vector<std::jthread> workers;
        const auto run = [&](int index) {
            try {
                band(rows * index / bands, rows * (index + 1) / bands);
            } catch (...) {
                errors[static_cast<std::size_t>(index)] = std::current_exception();
            }
        };
        for (int i = 1; i < bands; ++i) {
            workers.emplace_back(run, i);
        }
        run(0);
    }
    for (const std::exception_ptr& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

}  // namespace iv
