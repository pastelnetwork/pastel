#pragma once
// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <amount.h>
#include <rpc/protocol.h>
#include <uint256.h>

#include <list>
#include <map>
#include <stdint.h>
#include <string>
#include <memory>

#include <univalue.h>

class AsyncRPCQueue;
class CRPCCommand;

namespace RPCServer
{
    void OnStarted(std::function<void ()> slot);
    void OnStopped(std::function<void ()> slot);
    void OnPreCommand(std::function<void (const CRPCCommand&)> slot);
    void OnPostCommand(std::function<void (const CRPCCommand&)> slot);
}

class CBlockIndex;
class CNetAddr;

class JSONRequest
{
public:
    UniValue id;
    std::string strMethod;
    UniValue params;

    JSONRequest() { id = NullUniValue; }
    void parse(const UniValue& valRequest);
};

/** Query whether RPC is running */
bool IsRPCRunning();

/** Get the async queue*/
std::shared_ptr<AsyncRPCQueue> getAsyncRPCQueue();


/**
 * Set the RPC warmup status.  When this is done, all RPC calls will error out
 * immediately with RPC_IN_WARMUP.
 */
void SetRPCWarmupStatus(const std::string& newStatus);
/* Mark warmup as done.  RPC calls will be processed from now on.  */
void SetRPCWarmupFinished();

/* returns the current warmup state.  */
bool RPCIsInWarmup(std::string *statusOut);

/**
 * Type-check arguments; throws JSONRPCError if wrong type given. Does not check that
 * the right number of arguments are passed, just that any passed are the correct type.
 * Use like:  RPCTypeCheck(params, {(str_type), (int_type), (obj_type)});
 */
void RPCTypeCheck(const UniValue& params,
                  const std::list<UniValue::VType>& typesExpected, bool fAllowNull=false);

/*
  Check for expected keys/value types in an Object.
  Use like: RPCTypeCheckObj(object,{{"name", str_type}, {"value", int_type}});
*/
void RPCTypeCheckObj(const UniValue& o,
                  const std::map<std::string, UniValue::VType>& typesExpected, bool fAllowNull=false);

/** Opaque base class for timers returned by NewTimerFunc.
 * This provides no methods at the moment, but makes sure that delete
 * cleans up the whole state.
 */
class RPCTimerBase
{
public:
    virtual ~RPCTimerBase() {}
};

/**
 * RPC timer "driver".
 */
class RPCTimerInterface
{
public:
    virtual ~RPCTimerInterface() {}
    /** Implementation name */
    virtual const char *Name() = 0;
    /** Factory function for timers.
     * RPC will call the function to create a timer that will call func in *millis* milliseconds.
     * @note As the RPC mechanism is backend-neutral, it can use different implementations of timers.
     * This is needed to cope with the case in which there is no HTTP server, but
     * only GUI RPC console, and to break the dependency of rpcserver on httprpc.
     */
    virtual RPCTimerBase* NewTimer(std::function<void(void)>& func, int64_t millis) = 0;
};

/** Register factory function for timers */
void RPCRegisterTimerInterface(RPCTimerInterface *iface);
/** Unregister factory function for timers */
void RPCUnregisterTimerInterface(RPCTimerInterface *iface);

/**
 * Run func nSeconds from now.
 * Overrides previous timer <name> (if any).
 */
void RPCRunLater(const std::string& name, std::function<void(void)> func, int64_t nSeconds);

typedef UniValue(*rpcfn_type)(const UniValue& params, bool fHelp);

class CRPCCommand
{
public:
    std::string category;
    std::string name;
    rpcfn_type actor;
    bool okSafeMode;
};

/**
 * Bitcoin RPC command dispatcher.
 */
class CRPCTable
{
private:
    std::map<std::string, const CRPCCommand*> mapCommands;
public:
    CRPCTable();
    const CRPCCommand* operator[](const std::string& name) const;
    std::string help(const std::string& name) const;

    /**
     * Execute a method.
     * @param method   Method to execute
     * @param params   UniValue Array of arguments (JSON objects)
     * @returns Result of the call.
     * @throws an exception (UniValue) when an error happens.
     */
    UniValue execute(const std::string &method, const UniValue &params) const;


    /**
     * Appends a CRPCCommand to the dispatch table.
     * Returns false if RPC server is already running (dump concurrency protection).
     * Commands cannot be overwritten (returns false).
     */
    bool appendCommand(const std::string& name, const CRPCCommand* pcmd);
};

extern CRPCTable tableRPC;

/**
 * Utilities: convert hex-encoded Values
 * (throws error if not hex).
 */
extern uint256 ParseHashV(const UniValue& v, std::string strName);
extern uint256 ParseHashO(const UniValue& o, std::string strKey);
extern std::vector<unsigned char> ParseHexV(const UniValue& v, std::string strName);
extern std::vector<unsigned char> ParseHexO(const UniValue& o, std::string strKey);

extern int64_t nWalletUnlockTime;
extern CAmount AmountFromValue(const UniValue& value);
extern UniValue ValueFromAmount(const CAmount& amount);
extern double GetDifficulty(const CBlockIndex* blockindex = NULL);
extern double GetNetworkDifficulty(const CBlockIndex* blockindex = NULL);
extern std::string HelpRequiringPassphrase();
extern std::string HelpExampleCli(const std::string& methodname, const std::string& args);
extern std::string HelpExampleRpc(const std::string& methodname, const std::string& args);

extern void EnsureWalletIsUnlocked();

bool StartRPC();
void InterruptRPC();
void StopRPC();
std::string JSONRPCExecBatch(const UniValue& vReq);

extern std::string experimentalDisabledHelpMsg(const std::string& rpc, const std::string& enableArg);
