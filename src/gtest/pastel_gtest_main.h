#pragma once
#include <gtest/gtest.h>
#include <boost/thread.hpp>

#include "txdb.h"

void init_zksnark_params();

class CPastelTest_Environment : public ::testing::Environment
{
public:
    CPastelTest_Environment() = default;

    // initialize given network for unittests
    void InitializeChainTest(const CBaseChainParams::Network network);
    // finalize test network
    void FinalizeChainTest();

    // initialize regtest network for unittests
    void InitializeRegTest();
    // finalize regtest network
    void FinalizeRegTest() { FinalizeChainTest(); }
    // generate N coins
    void generate_coins(const size_t N);
    // generate unique temp directory and set it as a datadir
    std::string GenerateTempDataDir();
    // cleanup temp datadir
    void ClearTempDataDir();

protected:
    CCoinsViewDB* pcoinsdbview = nullptr;
    boost::thread_group threadGroup;
    fs::path m_TempDataDir; // generated temp datadir

    void SetUp() override;
    void TearDown() override;
};

extern CPastelTest_Environment *gl_pPastelTestEnv;
