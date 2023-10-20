#pragma once
// Copyright (c) 2012-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

/**
 * network protocol versioning
 */

inline constexpr int PROTOCOL_VERSION = 170010;

// min MasterNodes protocol version before Monet upgrade
inline constexpr int MN_MIN_PROTOCOL_VERSION = 170009;

//! initial proto version, to be increased after version/verack negotiation
inline constexpr int INIT_PROTO_VERSION = 209;

//! In this version, 'getheaders' was introduced.
inline constexpr int GETHEADERS_VERSION = 31800;

//! disconnect from peers older than this proto version
inline constexpr int MIN_PEER_PROTO_VERSION = 170008;

//! nTime field added to CAddress, starting with this version;
//! if possible, avoid requesting addresses nodes older than this
inline constexpr int CADDR_TIME_VERSION = 31402;

//! BIP 0031, pong message, is enabled for all versions AFTER this one
inline constexpr int BIP0031_VERSION = 60000;

//! "mempool" command, enhanced "getdata" behavior starts with this version
inline constexpr int MEMPOOL_GD_VERSION = 60002;

//! "filter*" commands are disabled without NODE_BLOOM after and including this version
inline constexpr int NO_BLOOM_VERSION = 170004;
