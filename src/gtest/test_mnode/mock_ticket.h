// Copyright (c) 2021-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#pragma once

#include <mnode/tickets/tickets-all.h>

#include <gmock/gmock.h>

class MockPastelIDRegTicket : public CPastelIDRegTicket
{
public:
    MOCK_METHOD(bool, VersionMgmt, (std::string & error, const bool bRead), (noexcept, override));
    MOCK_METHOD(ticket_validation_t, IsValid, (const TxOrigin txOrigin, const uint32_t nCallDepth), (const, noexcept, override));
    MOCK_METHOD(short, GetVersion, (), (const, noexcept, override));
    MOCK_METHOD(void, SerializationOp, (CDataStream & s, const SERIALIZE_ACTION ser_action), (override));
};

class MockChangeUserNameTicket : public CChangeUsernameTicket
{
public:
    MOCK_METHOD(bool, VersionMgmt, (std::string & error, const bool bRead), (noexcept, override));
    MOCK_METHOD(ticket_validation_t, IsValid, (const TxOrigin txOrigin, const uint32_t nCallDepth), (const, noexcept, override));
    MOCK_METHOD(short, GetVersion, (), (const, noexcept, override));
    MOCK_METHOD(void, SerializationOp, (CDataStream & s, const SERIALIZE_ACTION ser_action), (override));
};
