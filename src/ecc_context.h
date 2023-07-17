#pragma once
// Copyright (c) 2023 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <memory>
#include <mutex>

#include <secp256k1.h>

class ECCContext
{
private:
    static thread_local std::unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)> ctx_sign;
    static thread_local std::unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)> ctx_verify;
    static thread_local std::once_flag sign_context_init_flag;
    static thread_local std::once_flag verify_context_init_flag;

public:
    static secp256k1_context* getSignContext();
    static secp256k1_context* getVerifyContext();
};
