#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "chainparams.h"
#include "clientversion.h"
#include "deprecation.h"
#include "init.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"

#include <boost/bind/bind.hpp>
#include <fstream>

using namespace std;
using ::testing::StrictMock;
using namespace boost::placeholders;

static const string CLIENT_VERSION_STR = FormatVersion(CLIENT_VERSION);
extern atomic<bool> fRequestShutdown;

class MockUIInterface {
public:
    MOCK_METHOD3(ThreadSafeMessageBox, bool(const string& message,
                                      const string& caption,
                                      unsigned int style));
};

static bool ThreadSafeMessageBox(MockUIInterface *mock,
                                 const string& message,
                                 const string& caption,
                                 unsigned int style)
{
    return mock->ThreadSafeMessageBox(message, caption, style);
}

class DeprecationTest : public ::testing::Test {
protected:
    virtual void SetUp() {
        uiInterface.ThreadSafeMessageBox.disconnect_all_slots();
        uiInterface.ThreadSafeMessageBox.connect(boost::bind(ThreadSafeMessageBox, &mock_, _1, _2, _3));
        SelectParams(ChainNetwork::MAIN);
        
    }

    virtual void TearDown() {
        fRequestShutdown = false;
        mapArgs.clear();
    }

    StrictMock<MockUIInterface> mock_;

    static vector<string> read_lines(fs::path filepath) {
        vector<string> result;

        ifstream f(filepath.string().c_str());
        string line;
        while (getline(f,line)) {
            result.push_back(line);
        }

        return result;
    }
};

TEST_F(DeprecationTest, NonDeprecatedNodeKeepsRunning) {
    EXPECT_FALSE(ShutdownRequested());
    EnforceNodeDeprecation(DEPRECATION_HEIGHT - DEPRECATION_WARN_LIMIT - 1);
    EXPECT_FALSE(ShutdownRequested());
}

TEST_F(DeprecationTest, NodeNearDeprecationIsWarned) {
    EXPECT_FALSE(ShutdownRequested());
    EXPECT_CALL(mock_, ThreadSafeMessageBox(::testing::_, "", CClientUIInterface::MSG_WARNING));
    EnforceNodeDeprecation(DEPRECATION_HEIGHT - DEPRECATION_WARN_LIMIT);
    EXPECT_FALSE(ShutdownRequested());
}

TEST_F(DeprecationTest, NodeNearDeprecationWarningIsNotDuplicated) {
    EXPECT_FALSE(ShutdownRequested());
    EnforceNodeDeprecation(DEPRECATION_HEIGHT - DEPRECATION_WARN_LIMIT + 1);
    EXPECT_FALSE(ShutdownRequested());
}

TEST_F(DeprecationTest, NodeNearDeprecationWarningIsRepeatedOnStartup) {
    EXPECT_FALSE(ShutdownRequested());
    EXPECT_CALL(mock_, ThreadSafeMessageBox(::testing::_, "", CClientUIInterface::MSG_WARNING));
    EnforceNodeDeprecation(DEPRECATION_HEIGHT - DEPRECATION_WARN_LIMIT + 1, true);
    EXPECT_FALSE(ShutdownRequested());
}

TEST_F(DeprecationTest, DeprecatedNodeShutsDown) {
    EXPECT_FALSE(ShutdownRequested());
    EXPECT_CALL(mock_, ThreadSafeMessageBox(::testing::_, "", CClientUIInterface::MSG_ERROR));
    EnforceNodeDeprecation(DEPRECATION_HEIGHT);
    EXPECT_TRUE(ShutdownRequested());
}

TEST_F(DeprecationTest, DeprecatedNodeErrorIsNotDuplicated) {
    EXPECT_FALSE(ShutdownRequested());
    EnforceNodeDeprecation(DEPRECATION_HEIGHT + 1);
    EXPECT_TRUE(ShutdownRequested());
}

TEST_F(DeprecationTest, DeprecatedNodeErrorIsRepeatedOnStartup) {
    EXPECT_FALSE(ShutdownRequested());
    EXPECT_CALL(mock_, ThreadSafeMessageBox(::testing::_, "", CClientUIInterface::MSG_ERROR));
    EnforceNodeDeprecation(DEPRECATION_HEIGHT + 1, true);
    EXPECT_TRUE(ShutdownRequested());
}

TEST_F(DeprecationTest, DeprecatedNodeIgnoredOnRegtest) {
    SelectParams(ChainNetwork::REGTEST);
    EXPECT_FALSE(ShutdownRequested());
    EnforceNodeDeprecation(DEPRECATION_HEIGHT+1);
    EXPECT_FALSE(ShutdownRequested());
}

TEST_F(DeprecationTest, DeprecatedNodeIgnoredOnTestnet) {
    SelectParams(ChainNetwork::TESTNET);
    EXPECT_FALSE(ShutdownRequested());
    EnforceNodeDeprecation(DEPRECATION_HEIGHT+1);
    EXPECT_FALSE(ShutdownRequested());
}

TEST_F(DeprecationTest, AlertNotify) {
    fs::path temp = GetTempPath() /
        fs::unique_path("alertnotify-%%%%.txt");

    mapArgs["-alertnotify"] = string("echo %s >> ") + temp.string();

    EXPECT_CALL(mock_, ThreadSafeMessageBox(::testing::_, "", CClientUIInterface::MSG_WARNING));
    EnforceNodeDeprecation(DEPRECATION_HEIGHT - DEPRECATION_WARN_LIMIT, false, false);

    vector<string> r = read_lines(temp);
    EXPECT_EQ(r.size(), 1u);

    // -alertnotify restricts the message to safe characters.
    auto expectedMsg = strprintf(
        "This version will be deprecated at block height %d, and will automatically shut down. You should upgrade to the latest version of Zcash.",
        DEPRECATION_HEIGHT);

    // Windows built-in echo semantics are different than posixy shells. Quotes and
    // whitespace are printed literally.
#ifndef WIN32
    EXPECT_EQ(r[0], expectedMsg);
#else
    EXPECT_EQ(r[0], strprintf("'%s' ", expectedMsg));
#endif
    fs::remove(temp);
}
