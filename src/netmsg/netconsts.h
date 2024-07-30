#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>

/** The maximum number of entries in an 'inv' protocol message */
constexpr size_t MAX_INV_SZ = 50'000;
/** The maximum number of entries in an 'inv' protocol message to send */
constexpr size_t MAX_INV_SEND_SZ = 1'000;
/** The maximum number of entries in an 'addr' protocol message */
/** The maximum number of new addresses to accumulate before announcing. */
constexpr size_t MAX_ADDR_SZ = 1'000;
/** The maximum number of entries in an 'getdata' protocol message */
constexpr size_t MAX_GETDATA_SZ = 1'000;
/** The maximum number of entries in mapAskFor */
constexpr size_t MAPASKFOR_MAX_SZ = MAX_INV_SZ;
/** The maximum number of entries in setAskFor (larger due to getdata latency)*/
constexpr size_t SETASKFOR_MAX_SZ = 2 * MAX_INV_SZ;
/** The maximum number of peer connections to maintain. */
constexpr size_t DEFAULT_MAX_PEER_CONNECTIONS = 125;
/** Maximum length of incoming protocol messages (no message over 2 MiB is currently acceptable). */
constexpr unsigned int MAX_PROTOCOL_MESSAGE_LENGTH = 2 * 1024 * 1024;

enum class LocalAddressType : uint8_t
{
    NONE = 0,   // unknown
    IF,         // address a local interface listens on
    BIND,   // address explicit bound to
    UPNP,   // unused (was: address reported by UPnP)
    MANUAL, // address explicitly specified (-externalip=)

    MAX_COUNT
};
