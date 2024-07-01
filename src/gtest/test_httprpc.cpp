// Copyright (c) 2021-2024 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#include <gmock/gmock.h>

#include <utils/enum_util.h>
#include <httprpc.cpp>
#include <httpserver.h>

using namespace testing;

class MockHTTPRequest : public HTTPRequest
{
public:
    MOCK_METHOD(const CService&, GetPeer, (), (const, noexcept, override));
    MOCK_METHOD(RequestMethod, GetRequestMethod, (), (const, noexcept, override));
    MOCK_METHOD((std::pair<bool, std::string>), GetHeader, (const std::string& hdr), (const, noexcept, override));
    MOCK_METHOD(void, WriteHeader, (const std::string& hdr, const std::string& value), (override));
    MOCK_METHOD(void, WriteReply, (HTTPStatusCode status, const std::string& strReply), (override));

    MockHTTPRequest() : 
        HTTPRequest(-1, nullptr, 0)
    {}

    void CleanUp()
    {
        // So the parent destructor doesn't try to send a reply
        m_bReplySent = true;
    }
};

TEST(HTTPRPC, FailsOnGET) {
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(RequestMethod::GET));
    EXPECT_CALL(req, WriteReply(HTTPStatusCode::BAD_METHOD, "JSONRPC server handles only POST requests"))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}

TEST(HTTPRPC, FailsWithoutAuthHeader) {
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(RequestMethod::POST));
    EXPECT_CALL(req, GetHeader("authorization"))
        .WillRepeatedly(Return(std::make_pair(false, "")));
    EXPECT_CALL(req, WriteHeader("WWW-Authenticate", "Basic realm=\"jsonrpc\""))
        .Times(1);
    EXPECT_CALL(req, WriteReply(HTTPStatusCode::UNAUTHORIZED, ""))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}

TEST(HTTPRPC, FailsWithBadAuth)
{
    const CService mockService("127.0.0.1", 1337);
    MockHTTPRequest req;

    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(RequestMethod::POST));
    EXPECT_CALL(req, GetHeader("authorization"))
        .WillRepeatedly(Return(std::make_pair(true, "Basic spam:eggs")));
    EXPECT_CALL(req, GetPeer())
        .WillRepeatedly(ReturnRef(mockService));
    EXPECT_CALL(req, WriteHeader("WWW-Authenticate", "Basic realm=\"jsonrpc\""))
        .Times(1);
    EXPECT_CALL(req, WriteReply(HTTPStatusCode::UNAUTHORIZED, ""))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}
