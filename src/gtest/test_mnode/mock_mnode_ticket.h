#pragma once

#include "mnode/mnode-pastel.h"
#include <gmock/gmock.h>

class MockPastelIDRegTicket : public CPastelIDRegTicket
{
public:
    MOCK_METHOD(bool, VersionMgmt, (std::string & error, const bool bRead), (noexcept));
    MOCK_METHOD(bool, IsValid, (std::string & errRet, bool preReg, int depth), (const));
    MOCK_METHOD(short, GetVersion, (), (const, noexcept));
};
