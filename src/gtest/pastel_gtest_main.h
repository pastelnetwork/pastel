#pragma once
// Copyright (c) 2021-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <txdb/txdb.h>
#include <utils/svc_thread.h>

void init_zksnark_params();

class CPastelTest_Environment : public ::testing::Environment
{
public:
    CPastelTest_Environment() = default;

    // initialize given network for unittests
    void InitializeChainTest(const ChainNetwork network);
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
    void SetTempDataDir(const std::string& sDataDir);

    // cleanup temp datadir
    void ClearTempDataDir();

    fs::path GetTempDataDir() const noexcept { return m_TempDataDir; }

protected:
    std::unique_ptr<CCoinsViewDB> m_pCoinsDbView;
    CServiceThreadGroup threadGroup;
    fs::path m_TempDataDir; // generated temp datadir
    std::optional<ChainNetwork> m_TestNetwork;

    void SetUp() override;
    void TearDown() override;
};

extern CPastelTest_Environment *gl_pPastelTestEnv;
