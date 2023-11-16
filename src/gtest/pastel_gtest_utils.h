#pragma once
// Copyright (c) 2021-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>

#include <utils/uint256.h>
#include <consensus/params.h>

int GenZero(int n);
int GenMax(int n);

// generate random id
std::string generateRandomId(const size_t nLength);

// generate random txid
std::string generateRandomTxId();

// generate random uint256
uint256 generateRandomUint256();

// generate temporary filename with extension
// this is for test purposes only - no need to use mkstemp function
// or ensure that file is unique
std::string generateTempFileName(const char* szFileExt = ".tmp");

const Consensus::Params& RegtestActivateSapling();
void RegtestDeactivateSapling();