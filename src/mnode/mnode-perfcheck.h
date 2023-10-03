#pragma once
// Copyright (c) 2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <string>

using calibrate_benchmark_progress_fn = void (*)(const uint32_t nTrialNo, const uint32_t nNumTrials, const uint64_t nTotalResult) noexcept;

constexpr uint64_t CPU_BENCHMARK_THRESHOLD_MSECS = 400;

// Calibration function to get average benchmark result
uint64_t calibrateBenchmark(const uint32_t nNumTrials = 10, const uint32_t nNumIterations = 100,
    calibrate_benchmark_progress_fn pProgressFn = nullptr) noexcept;
uint64_t cpuBenchmark(const uint32_t nNumIterations = 100) noexcept;
bool checkHardwareRequirements(std::string& error, const char* szDesc);
