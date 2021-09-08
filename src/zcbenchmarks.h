#ifndef BENCHMARKS_H
#define BENCHMARKS_H

#ifndef _MSC_VER
#include <sys/time.h>
#endif
#include <stdlib.h>

extern double benchmark_sleep();
extern double benchmark_solve_equihash();
extern std::vector<double> benchmark_solve_equihash_threaded(int nThreads);
extern double benchmark_verify_equihash();
extern double benchmark_large_tx(size_t nInputs);
extern double benchmark_connectblock_slow();
extern double benchmark_sendtoaddress(CAmount amount);
extern double benchmark_loadwallet();
extern double benchmark_listunspent();
extern double benchmark_create_sapling_spend();
extern double benchmark_create_sapling_output();
extern double benchmark_verify_sapling_spend();
extern double benchmark_verify_sapling_output();

#endif
