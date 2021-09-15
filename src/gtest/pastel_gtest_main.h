#pragma once
#include <gtest/gtest.h>
#include <boost/thread.hpp>

#include "txdb.h"

class CPastelTest_Environment : public ::testing::Environment
{
public:
    CPastelTest_Environment() = default;

    void InitializeRegTest();
    void FinalizeRegTest();
    void generate_coins(const size_t N);

protected:
    CCoinsViewDB* pcoinsdbview = nullptr;
    boost::thread_group threadGroup;

    void SetUp() override;
    void TearDown() override;
};

extern CPastelTest_Environment *gl_pPastelTestEnv;
