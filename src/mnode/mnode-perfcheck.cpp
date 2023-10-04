// Copyright (c) 2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <random>

#include <util.h>
#include <str_utils.h>
#include <mnode/mnode-perfcheck.h>

using namespace std;

// Minimum hardware requirements
constexpr unsigned int HARDWARE_REQUIREMENTS_MIN_CORES = 6;
const uint64_t HARDWARE_REQUIREMENTS_MIN_TOTAL_RAM = 24ULL * 1024 * 1024 * 1024; // 24GB

// Compute Fibonacci number recursively.
int fibonacci(int n) noexcept
{
    if (n <= 1)
        return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

// Sort a large vector.
void sortVector(const uint32_t nVectorSize = 100'000) noexcept
{
    v_ints v(nVectorSize);
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<uint32_t> distr(0, nVectorSize);
    for (auto &elem : v)
        elem = distr(gen);
    sort(v.begin(), v.end());
}

// Perform matrix multiplication.
void matrixMultiplication(const uint32_t nMatrixSize = 100) noexcept
{
    vector<v_ints> A(nMatrixSize, v_ints(nMatrixSize));
    vector<v_ints> B(nMatrixSize, v_ints(nMatrixSize));
    vector<v_ints> C(nMatrixSize, v_ints(nMatrixSize, 0));
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution distr(-100, 100);
    for (uint32_t i = 0; i < nMatrixSize; ++i)
    {
        for (uint32_t j = 0; j < nMatrixSize; ++j)
        {
            A[i][j] = distr(gen);
            B[i][j] = distr(gen);
        }
    }
    for (uint32_t i = 0; i < nMatrixSize; ++i)
        for (uint32_t j = 0; j < nMatrixSize; ++j)
            for (uint32_t k = 0; k < nMatrixSize; ++k)
                C[i][j] += A[i][k] * B[k][j];
}

// Compute dot product of two large vectors.
int dotProduct(const uint32_t nVectorSize = 100'000) noexcept
{
    v_ints A(nVectorSize);
    v_ints B(nVectorSize);
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution distr(-100, 100);
    for (uint32_t i = 0; i < nVectorSize; ++i)
    {
        A[i] = distr(gen);
        B[i] = distr(gen);
    }
    int result = 0;
    for (uint32_t i = 0; i < nVectorSize; ++i)
        result += A[i] * B[i];
    return result;
}

uint64_t cpuBenchmark(const uint32_t nNumIterations) noexcept
{
    chrono::duration<double> totalElapsed(0);
    for (uint32_t i = 1; i <= nNumIterations; ++i)
    {
        const auto start_time = chrono::high_resolution_clock::now();
        fibonacci(35);
        sortVector();
        matrixMultiplication();
        dotProduct();
        const auto end_time = chrono::high_resolution_clock::now();
        totalElapsed += (end_time - start_time);
    }
    double avgElapsed = totalElapsed.count() / nNumIterations;
    return static_cast<uint64_t>(avgElapsed * 1000);
}


// Calibration function to get average benchmark result
uint64_t calibrateBenchmark(const uint32_t nNumTrials, const uint32_t nNumIterations, 
    calibrate_benchmark_progress_fn pProgressFn) noexcept
{
    uint64_t nTotalResult = 0;
    for (uint32_t i = 1; i <= nNumTrials; ++i)
    {
        nTotalResult += cpuBenchmark(nNumIterations);
        if (pProgressFn)
			pProgressFn(i, nNumTrials, nTotalResult);
    }
    return nTotalResult / nNumTrials;
}

bool checkHardwareRequirements(string &error, const char *szDesc)
{
    const unsigned int nNumCores = GetNumCores();
	const uint64_t nTotalRAMBytes = GetTotalPhysicalMemory();

    if (nNumCores < HARDWARE_REQUIREMENTS_MIN_CORES || nTotalRAMBytes < HARDWARE_REQUIREMENTS_MIN_TOTAL_RAM)
    {
		error = strprintf("Machine does not meet the minimum requirements for %s:", SAFE_SZ(szDesc));
        bool bCPUReqFailed = false;
        if (nNumCores < HARDWARE_REQUIREMENTS_MIN_CORES)
        {
            error += strprintf(" CPU cores: %u/%u", nNumCores, HARDWARE_REQUIREMENTS_MIN_CORES);
            bCPUReqFailed = true;
        }
        if (nTotalRAMBytes < HARDWARE_REQUIREMENTS_MIN_TOTAL_RAM)
        {
            if (bCPUReqFailed)
                error += ";";
            error += strprintf(" RAM: %u Mb /%u Mb", nTotalRAMBytes / (1024 * 1024), HARDWARE_REQUIREMENTS_MIN_TOTAL_RAM / (1024 * 1024));
        }
		return false;
	}
    LogPrintf("Machine meets the minimum requirements for %s:\n"
        "   CPU cores: %u / %u\n"
        "   RAM: %u Mb / %u Mb\n",
        SAFE_SZ(szDesc), nNumCores, HARDWARE_REQUIREMENTS_MIN_CORES,
        nTotalRAMBytes / (1024 * 1024), HARDWARE_REQUIREMENTS_MIN_TOTAL_RAM / (1024 * 1024));
	return true;
}
