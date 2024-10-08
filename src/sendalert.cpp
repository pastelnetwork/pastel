// Copyright (c) 2016-2018 The Zcash developers
// Copyright (c) 2018-2024 The Pastel Core developers
// 
// Original code from: https://gist.github.com/laanwj/0e689cfa37b52bcbbb44

/*

To set up a new alert system
----------------------------

Create a new alert key pair:
openssl ecparam -name secp256k1 -genkey -param_enc explicit -outform PEM -out data.pem

Get the private key in hex:
openssl ec -in data.pem -outform DER | tail -c 279 | xxd -p -c 279

Get the public key in hex:
openssl ec -in data.pem -pubout -outform DER | tail -c 65 | xxd -p -c 65

Update the public keys found in chainparams.cpp.


To send an alert message
------------------------

Copy the private keys into alertkeys.h.

Modify the alert parameters, id and message found in this file.

Build and run with -sendalert or -printalert.

./pasteld -printtoconsole -sendalert

One minute after starting up, the alert will be broadcast. It is then
flooded through the network until the nRelayUntil time, and will be
active until nExpiration OR the alert is cancelled.

If you make a mistake, send another alert with nCancel set to cancel
the bad alert.

*/

#include <utils/util.h>
#include <utils/utiltime.h>
#include <main.h>
#include <net.h>
#include <alert.h>
#include <init.h>
#include <key.h>
#include <clientversion.h>
#include <chainparams.h>

#include <alertkeys.h>
#include <netmsg/nodemanager.h>

using namespace std;

void ThreadSendAlert()
{
    if (!mapArgs.count("-sendalert") && !mapArgs.count("-printalert"))
        return;

    MilliSleep(60*1000); // Wait a minute so we get connected

    //
    // Alerts are relayed around the network until nRelayUntil, flood
    // filling to every node.
    // After the relay time is past, new nodes are told about alerts
    // when they connect to peers, until either nExpiration or
    // the alert is cancelled by a newer alert.
    // Nodes never save alerts to disk, they are in-memory-only.
    //
    CAlert alert;
    alert.nRelayUntil   = GetTime() + 15 * 60;
    alert.nExpiration   = GetTime() + 10 * 365 * DAYS;
    alert.nID           = 1014;  // alert ID
    alert.nCancel       = 1013;  // cancels previous messages up to this ID number

    // These versions are protocol versions
    // 170002 : 1.0.0
    // 170006 : 1.1.2
    // 170007 : 2.0.0
    // 170008 : Sapling
    // 170009 : Cezanne v1.1.4 (1007,1008) 
    // 170010 : Monet   v2.0.0 (1009,1010)
    // 170011 : Vermeer v2.1.0 (1011,1012)
    // 170012 : Matisse v2.2.0 (1013,1014)
    alert.nMinVer       = 170010;
    alert.nMaxVer       = 170011;

    //
    // main.cpp:
    //  1000 for Misc warnings like out of disk space and clock is wrong
    //  2000 for longer invalid proof-of-work chain
    //  Higher numbers mean higher priority
    //  4000 or higher will put the RPC into safe mode
    alert.nPriority     = ALERT_PRIORITY_SAFE_MODE;
    // alert.nPriority     = 1000;
    alert.strComment    = "";
    alert.strStatusBar  = "WARNING: You are running a version that is no longer compatible-- upgrade your node here, or you won't be able to connect to the network: https://github.com/PastelNetwork/Pastel";
    alert.strRPCError   = alert.strStatusBar;

    // Set specific client version/versions here. If setSubVer is empty, no filtering on subver is done:
    // alert.setSubVer.insert(string("/MagicBean:1.1.2/"));
    const v_strings useragents = {}; //{"MagicBean", "BeanStalk", "AppleSeed", "EleosZcash"};

    for (const auto& useragent : useragents)
    {}

    // Sanity check
    assert(alert.strComment.length() <= 65536); // max length in alert.h
    assert(alert.strStatusBar.length() <= 256);
    assert(alert.strRPCError.length() <= 256);

    // Sign
    const CChainParams& chainparams = Params();
    const char* szPrivKey = nullptr;
    if (chainparams.IsMainNet())
        szPrivKey = pszPrivKey;
    else if (chainparams.IsTestNet())
        szPrivKey = pszTestNetPrivKey;
    else if (chainparams.IsDevNet())
        szPrivKey = pszDevNetPrivKey;
    else if (chainparams.IsRegTest())
        szPrivKey = pszRegTestPrivKey;
    else
    {
        printf("ThreadSendAlert() : cannot retrieve alert private key, unknown network type\n");
        return;
    }
    v_uint8 vchTmp(ParseHex(szPrivKey));
    CPrivKey privKey(vchTmp.begin(), vchTmp.end());

    CDataStream sMsg(SER_NETWORK, CLIENT_VERSION);
    sMsg << *(CUnsignedAlert*)&alert;
    alert.vchMsg = v_uint8(sMsg.begin(), sMsg.end());
    CKey key;
    if (!key.SetPrivKey(privKey, false))
    {
        printf("ThreadSendAlert() : key.SetPrivKey failed\n");
        return;
    }
    if (!key.Sign(Hash(alert.vchMsg.begin(), alert.vchMsg.end()), alert.vchSig))
    {
        printf("ThreadSendAlert() : key.Sign failed\n");
        return;
    }

    // Test
    CDataStream sBuffer(SER_NETWORK, CLIENT_VERSION);
    sBuffer << alert;
    CAlert alert2;
    sBuffer >> alert2;
    if (!alert2.CheckSignature(chainparams.AlertKey()))
    {
        printf("ThreadSendAlert() : CheckSignature failed\n");
        return;
    }
    assert(alert2.vchMsg == alert.vchMsg);
    assert(alert2.vchSig == alert.vchSig);
    alert.SetNull();
    printf("\nThreadSendAlert:\n");
    printf("hash=%s\n", alert2.GetHash().ToString().c_str());
    printf("%s\n", alert2.ToString().c_str());
    printf("vchMsg=%s\n", HexStr(alert2.vchMsg).c_str());
    printf("vchSig=%s\n", HexStr(alert2.vchSig).c_str());

    // Confirm
    if (!mapArgs.count("-sendalert"))
        return;
    while ((gl_NodeManager.GetNodeCount() == 0) && !IsShutdownRequested())
        MilliSleep(500);
    if (IsShutdownRequested())
        return;

    // Send
    printf("ThreadSendAlert() : Sending alert\n");
    int nSent = 0;
    {
        node_vector_t vNodesCopy = gl_NodeManager.CopyNodes();
        for (auto &pnode : vNodesCopy)
        {
            if (alert2.RelayTo(pnode))
            {
                printf("ThreadSendAlert() : Sent alert to %s\n", pnode->addr.ToString().c_str());
                nSent++;
            }
        }
    }
    printf("ThreadSendAlert() : Alert sent to %d nodes\n", nSent);
}
