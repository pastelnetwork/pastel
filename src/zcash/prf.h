/*
Zcash uses SHA256Compress as a PRF for various components
within the zkSNARK circuit.
*/
#pragma once
#include <array>

#include <utils/uint256.h>
#include <utils/uint252.h>

//! Sprout functions
uint256 PRF_addr_a_pk(const uint252& a_sk);
uint256 PRF_addr_sk_enc(const uint252& a_sk);
uint256 PRF_nf(const uint252& a_sk, const uint256& rho);
uint256 PRF_pk(const uint252& a_sk, size_t i0, const uint256& h_sig);
uint256 PRF_rho(const uint252& phi, size_t i0, const uint256& h_sig);

//! Sapling functions
uint256 PRF_ask(const uint256& sk);
uint256 PRF_nsk(const uint256& sk);
uint256 PRF_ovk(const uint256& sk);

std::array<unsigned char, 11> default_diversifier(const uint256& sk);
