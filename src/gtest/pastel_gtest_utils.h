#pragma once
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or httpa://www.opensource.org/licenses/mit-license.php.

#include <string>

int GenZero(int n);
int GenMax(int n);

// generate random id
std::string generateRandomId(const size_t nLength);

// generate temporary filename with extension
// this is for test purposes only - no need to use mkstemp function
// or ensure that file is unique
std::string generateTempFileName(const char* szFileExt = ".tmp");
