// Copyright (c) 2023 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <vector>

#include <support/allocators/secure.h>
#include <random.h>
#include <ecc_context.h>

using namespace std;

secp256k1_context* ECCContext::getSignContext()
{
    // Initialize the elliptic curve support - called once per thread
    call_once(sign_context_init_flag, []
    {
		ctx_sign.reset(secp256k1_context_create(SECP256K1_CONTEXT_SIGN));
        // Ensure the randomization function is only called if the context creation was successful
        if (!ctx_sign)
            throw runtime_error("ECCContext::getSignContext() : secp256k1_context_create failed");
        // Pass in a random blinding seed to the secp256k1 context.
        vector<unsigned char, secure_allocator<unsigned char>> vseed(32);
        GetRandBytes(vseed.data(), 32);
        if (secp256k1_context_randomize(ctx_sign.get(), vseed.data()) != 1)
			throw runtime_error("ECCContext::getSignContext() : secp256k1_context_randomize failed");
	});
	return ctx_sign.get();
}

secp256k1_context* ECCContext::getVerifyContext()
{
    call_once(verify_context_init_flag, []
    {
		ctx_verify.reset(secp256k1_context_create(SECP256K1_CONTEXT_VERIFY));
	});
	return ctx_verify.get();
}
thread_local unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)> ECCContext::ctx_sign{nullptr, &secp256k1_context_destroy};
thread_local unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)> ECCContext::ctx_verify{nullptr, &secp256k1_context_destroy};
thread_local once_flag ECCContext::sign_context_init_flag;
thread_local once_flag ECCContext::verify_context_init_flag;
