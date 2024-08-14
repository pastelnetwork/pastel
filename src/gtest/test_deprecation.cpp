#include <fstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <utils/util.h>
#include <utils/utilstrencodings.h>
#include <utils/vector_types.h>
#include <chainparams.h>
#include <clientversion.h>
#include <deprecation.h>
#include <init.h>
#include <ui_interface.h>

using namespace std;
using namespace testing;

static const string CLIENT_VERSION_STR = FormatVersion(CLIENT_VERSION);
extern atomic_bool fRequestShutdown;

class MockUIInterface {
public:
    MOCK_METHOD(bool, ThreadSafeMessageBox, (const string& message,
                                      const string& caption,
                                      unsigned int style), ());
};

class DeprecationTest : public Test
{
protected:
    void SetUp() override
    {
        uiInterface.ThreadSafeMessageBox.disconnect_all_slots();
        uiInterface.ThreadSafeMessageBox.connect([this]
        (const string& message, const string& caption, unsigned int style) -> bool
        {
            return mock_.ThreadSafeMessageBox(message, caption, style);
        });
        SelectParams(ChainNetwork::MAIN);
        
    }

    void TearDown() override
    {
        fRequestShutdown = false;
        mapArgs.clear();
    }

    StrictMock<MockUIInterface> mock_;

    static v_strings read_lines(fs::path filepath)
    {
        v_strings result;

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
    EXPECT_CALL(mock_, ThreadSafeMessageBox(_, "", CClientUIInterface::MSG_WARNING));
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
    EXPECT_CALL(mock_, ThreadSafeMessageBox(_, "", CClientUIInterface::MSG_WARNING));
    EnforceNodeDeprecation(DEPRECATION_HEIGHT - DEPRECATION_WARN_LIMIT + 1, true);
    EXPECT_FALSE(ShutdownRequested());
}

TEST_F(DeprecationTest, DeprecatedNodeShutsDown) {
    EXPECT_FALSE(ShutdownRequested());
    EXPECT_CALL(mock_, ThreadSafeMessageBox(_, "", CClientUIInterface::MSG_ERROR));
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
    EXPECT_CALL(mock_, ThreadSafeMessageBox(_, "", CClientUIInterface::MSG_ERROR));
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

TEST_F(DeprecationTest, AlertNotify)
{
    fs::path temp = GetTempPath() /
        fs::unique_path("alertnotify-%%%%.txt");

    mapArgs["-alertnotify"] = string("echo %s >> ") + temp.string();

    EXPECT_CALL(mock_, ThreadSafeMessageBox(_, "", CClientUIInterface::MSG_WARNING));
    EnforceNodeDeprecation(DEPRECATION_HEIGHT - DEPRECATION_WARN_LIMIT, false, false);

    vector<string> r = read_lines(temp);
    EXPECT_EQ(r.size(), 1u);

    // -alertnotify restricts the message to safe characters.
    auto expectedMsg = strprintf(
        "This version will be deprecated at block height %d, and will automatically shut down. You should upgrade to the latest version of Pastel.",
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
