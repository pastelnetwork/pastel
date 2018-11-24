#!/bin/bash
set -e -o pipefail

CURDIR=$(cd $(dirname "$0"); pwd)
# Get BUILDDIR and REAL_ANIMECOIND
. "${CURDIR}/tests-config.sh"

export ANIMECOINCLI=${BUILDDIR}/qa/pull-tester/run-bitcoin-cli
export ANIMECOIND=${REAL_ANIMECOIND}

#Run the tests

testScripts=(
    'paymentdisclosure.py'            #Good
    'prioritisetransaction.py'        #Good
    'wallet_treestate.py'             #Good
    'wallet_protectcoinbase.py'       #Good
    'wallet_shieldcoinbase.py'        #Good
    'wallet.py'                       #Good
    # 'wallet_nullifiers.py'            #FIXME - wallet_nullifiers.py", line 220
    # 'wallet_1941.py'                  #FIXME - wallet_1941.py", line 52
    # 'listtransactions.py'             #FIXME - listtransactions.py", line 38
    # 'mempool_resurrect_test.py'       #FIXME - mempool_resurrect_test.py", line 45
    # 'txn_doublespend.py'              #FIXME - txn_doublespend.py", line 32
    # 'txn_doublespend.py --mineblock'  #FIXME - txn_doublespend.py", line 32
    # 'getchaintips.py'                 #FIXME - getchaintips.py", line 16
    'rawtransactions.py'              #Good
    # 'rest.py'                         #FIXME - rest.py", line 240
    # 'mempool_spendcoinbase.py'        #FIXME - mempool_spendcoinbase.py", line 42
    # 'mempool_coinbase_spends.py'      #FIXME - mempool_coinbase_spends.py", line 57
    'mempool_tx_input_limit.py'       #Good
    'httpbasics.py'                   #Good
    'zapwallettxes.py'                #Good
    'proxy_test.py'                   #Good
    'merkle_blocks.py'                #Good
    'fundrawtransaction.py'           #Good
    'signrawtransactions.py'          #Good
    'walletbackup.py'                 #Good
    'key_import_export.py'            #Good
    'nodehandling.py'                 #Good
    'reindex.py'                      #Good
    'decodescript.py'                 #Good
    'disablewallet.py'                #Good
    # 'zcjoinsplit.py'                  #FIXME - zcjoinsplit.py", line 29
    # 'zcjoinsplitdoublespend.py'       #???
    # 'zkey_import_export.py'           #FIXME - 
    'getblocktemplate.py'             #Good
    # 'bip65-cltv-p2p.py'             #FIXME
    # 'bipdersig-p2p.py'              #FIXME
    'mn_main.py'                     #Good
    'mn_payment.py'                  #Good
    'mn_governance.py'
);
testScriptsExt=(
    'getblocktemplate_longpoll.py'    #Good
    # 'getblocktemplate_proposals.py' #FIXME
    # 'pruning.py'                    #FIXME
    'forknotify.py'                   #Good
    # 'hardforkdetection.py'          #FIXME
    # 'invalidateblock.py'            #FIXME
    'keypool.py'                      #Good
    # 'receivedby.py'                 #FIXME
    'rpcbind_test.py'                 #Good
#   'script_test.py'                  #???
    # 'smartfees.py'                  #FIXME
    'maxblocksinflight.py'            #Good
    # 'invalidblockrequest.py'        #FIXME
    # 'p2p-acceptblock.py'            #FIXME
);

if [ "x$ENABLE_ZMQ" = "x1" ]; then
#    testScripts+=('zmq_test.py')          #FIXME - hangs
fi

if [ "x$ENABLE_PROTON" = "x1" ]; then
  testScripts+=('proton_test.py')
fi

extArg="-extended"
passOn=${@#$extArg}

successCount=0
declare -a failures

function runTestScript
{
    local testName="$1"
    shift

    echo -e "=== Running testscript ${testName} ==="

    if eval "$@"
    then
        successCount=$(expr $successCount + 1)
        echo "--- Success: ${testName} ---"
    else
        failures[${#failures[@]}]="$testName"
        echo "!!! FAIL: ${testName} !!!"
    fi

    echo
}

if [ "x${ENABLE_BITCOIND}${ENABLE_UTILS}${ENABLE_WALLET}" = "x111" ]; then
    for (( i = 0; i < ${#testScripts[@]}; i++ ))
    do
        if [ -z "$1" ] || [ "${1:0:1}" == "-" ] || [ "$1" == "${testScripts[$i]}" ] || [ "$1.py" == "${testScripts[$i]}" ]
        then
            runTestScript \
                "${testScripts[$i]}" \
                "${BUILDDIR}/qa/rpc-tests/${testScripts[$i]}" \
                --srcdir "${BUILDDIR}/src" ${passOn}
        fi
    done   
    for (( i = 0; i < ${#testScriptsExt[@]}; i++ ))
    do
        if [ "$1" == $extArg ] || [ "$1" == "${testScriptsExt[$i]}" ] || [ "$1.py" == "${testScriptsExt[$i]}" ]
        then
            runTestScript \
                "${testScriptsExt[$i]}" \
                "${BUILDDIR}/qa/rpc-tests/${testScriptsExt[$i]}" \
                --srcdir "${BUILDDIR}/src" ${passOn}
        fi
    done

    echo -e "\n\nTests completed: $(expr $successCount + ${#failures[@]})"
    echo "successes $successCount; failures: ${#failures[@]}"

    if [ ${#failures[@]} -gt 0 ]
    then
        echo -e "\nFailing tests: ${failures[*]}"
        exit 1
    else
        exit 0
    fi
else
  echo "No rpc tests to run. Wallet, utils, and bitcoind must all be enabled"
fi
