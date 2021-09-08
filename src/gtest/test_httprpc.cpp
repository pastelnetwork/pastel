#include <gmock/gmock.h>

#include "httprpc.cpp"
#include "httpserver.h"

using ::testing::Return;

class MockHTTPRequest : public HTTPRequest {
public:
    MOCK_METHOD(CService, GetPeer, (), ());
    MOCK_METHOD(HTTPRequest::RequestMethod, GetRequestMethod, (), ());
    MOCK_METHOD((std::pair<bool, std::string>), GetHeader, (const std::string& hdr), ());
    MOCK_METHOD(void, WriteHeader, (const std::string& hdr, const std::string& value), ());
    MOCK_METHOD(void, WriteReply, (int nStatus, const std::string& strReply), ());

    MockHTTPRequest() : HTTPRequest(nullptr) {}
    void CleanUp() {
        // So the parent destructor doesn't try to send a reply
        replySent = true;
    }
};

TEST(HTTPRPC, FailsOnGET) {
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(HTTPRequest::GET));
    EXPECT_CALL(req, WriteReply(HTTP_BAD_METHOD, "JSONRPC server handles only POST requests"))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}

TEST(HTTPRPC, FailsWithoutAuthHeader) {
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(HTTPRequest::POST));
    EXPECT_CALL(req, GetHeader("authorization"))
        .WillRepeatedly(Return(std::make_pair(false, "")));
    EXPECT_CALL(req, WriteHeader("WWW-Authenticate", "Basic realm=\"jsonrpc\""))
        .Times(1);
    EXPECT_CALL(req, WriteReply(HTTP_UNAUTHORIZED, ""))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}

TEST(HTTPRPC, FailsWithBadAuth) {
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(HTTPRequest::POST));
    EXPECT_CALL(req, GetHeader("authorization"))
        .WillRepeatedly(Return(std::make_pair(true, "Basic spam:eggs")));
    EXPECT_CALL(req, GetPeer())
        .WillRepeatedly(Return(CService("127.0.0.1:1337")));
    EXPECT_CALL(req, WriteHeader("WWW-Authenticate", "Basic realm=\"jsonrpc\""))
        .Times(1);
    EXPECT_CALL(req, WriteReply(HTTP_UNAUTHORIZED, ""))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}
