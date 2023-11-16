// Copyright (c) 2022-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <utils/svc_thread.h>

using namespace std;
using namespace testing;

constexpr auto TEST_THREAD_NAME = "TestThread";

class CTestThread : 
	public CServiceThread,
	public Test
{
public:
	enum class TEST_MODE
	{
		not_defined,
		simple_exec,
		stoppable_exec
	};
	CTestThread() : 
		CServiceThread(TEST_THREAD_NAME),
		m_bTestVar(false),
		m_nTestVar(0),
		m_TestMode(TEST_MODE::not_defined)
	{}

	void execute() override
	{
		switch (m_TestMode)
		{
		case TEST_MODE::simple_exec:
			m_bTestVar = true;
			break;

		case TEST_MODE::stoppable_exec:
			while (!shouldStop())
			{
				++m_nTestVar;
				this_thread::sleep_for(200ms);
			}
			break;

		default:
			break;
		}
	}

protected:
	bool m_bTestVar;
	int m_nTestVar;
	TEST_MODE m_TestMode;
};

TEST_F(CTestThread, ctor)
{
	EXPECT_TRUE(m_bTrace);
	setTrace(false);
	EXPECT_FALSE(m_bTrace);

	EXPECT_FALSE(m_bRunning);
	EXPECT_FALSE(m_bStopRequested);
	EXPECT_TRUE(!m_sThreadName.empty());

	EXPECT_FALSE(shouldStop());
	EXPECT_FALSE(isRunning());
	EXPECT_TRUE(!get_thread_name().empty());
}

// simple thread execution
TEST_F(CTestThread, exec)
{
	m_bTestVar = false;
	m_TestMode = TEST_MODE::simple_exec;
	EXPECT_TRUE(start());
	waitForStop();
	EXPECT_TRUE(m_bTestVar);
}

// thread with interrupt support
TEST_F(CTestThread, exec_stoppable)
{
	m_nTestVar = 0;
	m_TestMode = TEST_MODE::stoppable_exec;
	EXPECT_TRUE(start());
	this_thread::sleep_for(200ms);
	EXPECT_TRUE(isRunning());
	this_thread::sleep_for(1s);
	EXPECT_TRUE(isRunning());
	EXPECT_FALSE(shouldStop());
	stop(); // request stop
	EXPECT_TRUE(shouldStop());
	this_thread::sleep_for(500ms);
	EXPECT_FALSE(isRunning());
	waitForStop();
	EXPECT_GE(m_nTestVar, 5);
}

// CFuncThread tests
void test_standalone_fn(int &n)
{
	n = 42;
}

TEST(svc_thread, exec_standalone)
{
	int n = 0;
	auto func = bind(&test_standalone_fn, std::ref(n));
	CFuncThread<decltype(func)> fn(TEST_THREAD_NAME, func);
	fn.start();
	fn.waitForStop();
	EXPECT_EQ(n, 42);
}

// CServiceThreadGroup
class TestServiceThreadGroup : 
	public CServiceThreadGroup,
	public Test
{
public:
	TestServiceThreadGroup()
	{}
};

void test_standalone_group_fn(atomic_int64_t &counter)
{
	counter++;
}

TEST_F(TestServiceThreadGroup, exec_func_thread)
{
	constexpr size_t nThreadCount = 20;

	atomic_int64_t counter = 0;
	for (size_t i = 0; i < nThreadCount; ++i)
	{
		const size_t nID = add_func_thread(strprintf("test-%zu", i + 1).c_str(), bind(&test_standalone_group_fn, std::ref(counter)), true);
		EXPECT_GT(nID, 0);
	}
	join_all();
	EXPECT_EQ(counter, nThreadCount);
}
