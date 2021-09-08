#!/bin/bash
set -e -o pipefail

CURDIR=$(cd $(dirname "$0"); pwd)
# Get BUILDDIR and REAL_PASTELD
. "${CURDIR}/tests-config.sh"

export PASTELCLI=${BUILDDIR}/qa/pull-tester/run-pastel-cli
export PASTELD=${REAL_PASTELD}

function show_usage()
{
    cat <<EOF
Pastel RPC-test utility
Usage:

$0 [-h || --help]
  Show this help message and exit.

$0 [-g || --group]=test_script_group [test_script_params]
  Execute group of test scripts "test_script_group".
  test_script_params - script parameters that will be passed to each script on execution.

$0 [-n || --name]=test_script_name [test_script_params]
  Execute test script name [test_script_name] with parameters [test_script_params]

$0 [test_script_name] [test_script_params]

[test_script_params] - passed to python test script as is
  --nocleanup     Leave pasteld and test* data directories and exit or error.
  --noshutdown    Don't stop pasteld processes after the test execution.
  --tracerpc      Print out all RPC calls as they are made.
  --srcdir=SRCDIR Source directory containing pasteld/pastel-cli (default: ../../src).
  --tmpdir=TMPDIR Root directory for all temp data dirs.
EOF
}

# groups of test scripts - can be executed independently
declare -a testScripts=(
    'framework.py'
    'prioritisetransaction.py'
    'wallet_anchorfork.py'
    'wallet_changeindicator.py'
    'wallet_import_export.py'
    'wallet_protectcoinbase.py'
    'wallet_shieldcoinbase_sapling.py'
    'wallet_listreceived.py'
    'wallet.py'
    'wallet_nullifiers.py'
    'wallet_1941.py'
    'wallet_addresses.py'
    'wallet_sapling.py'
    'wallet_listnotes.py'
    'mergetoaddress_sapling.py'
    'listtransactions.py'
    'mempool_resurrect_test.py'
    'txn_doublespend.py'
    'txn_doublespend.py --mineblock'
    'getchaintips.py'
    'rawtransactions.py'
    'rest.py'
    'mempool_spendcoinbase.py'
    'mempool_reorg.py'
    'mempool_tx_expiry.py'
    'httpbasics.py'
    'zapwallettxes.py'
    'proxy_test.py'
    'merkle_blocks.py'
    'fundrawtransaction.py'
    'signrawtransactions.py'
    'signrawtransaction_offline.py'
    'walletbackup.py'
    'key_import_export.py'
    'nodehandling.py'
    'reindex.py'
    'decodescript.py'
    'blockchain.py'
    'disablewallet.py'
    'zkey_import_export.py' 
    'reorg_limit.py'
    'getblocktemplate.py'
    'bip65-cltv-p2p.py'
    'bipdersig-p2p.py'
    'rewind_index.py'
    'p2p_txexpiry_dos.py'
    'p2p_txexpiringsoon.py'
    'p2p_node_bloom.py'
    'regtest_signrawtransaction.py'
    'finalsaplingroot.py'
)

declare -a testScriptsToFix=(
    'wallet_persistence.py' #fails
    'mempool_nu_activation.py' #timesout
    'getblocktemplate_proposals.py'
    'pruning.py'                    
    'hardforkdetection.py'          
    'invalidateblock.py'            
    'invalidblockrequest.py'        
    'receivedby.py'                 
    'script_test.py'
    'smartfees.py'                  
    'invalidblockrequest.py'        
    'p2p-acceptblock.py'            
    'keypool.py'
    'rpcbind_test.py'
)

declare -a testScriptsMN=(
    'mn_main.py'
    'mn_bugs.py'
    'mn_payment.py'
    'mn_governance.py'
    'mn_tickets.py'
    'mn_tickets_validation.py'
    'mn_messaging.py'
)

declare -a testScriptsExt=(
    'getblocktemplate_longpoll.py'
    'forknotify.py'
    'maxblocksinflight.py'
);

#if [ "x$ENABLE_ZMQ" = "x1" ]; then
    # echo -e "=== ZMQ test disabled ==="
    # testScripts+=('zmq_test.py')      #FIXME - hangs
#fi

if [ "x$ENABLE_PROTON" = "x1" ]; then
  testScripts+=('proton_test.py')     #???  
fi

function get_time()
{
  echo "$(date +%s.%N)"
}

# Calculate time difference and format it as HH:MM:SS.msecs
#  parameters:
#    $1: time_end
#    $2: time_start
function difftime()
{
	local secs=$(($((10#${1%.*})) - $((10#${2%.*}))))
	local nsecs=$(($((10#${1#*.})) - $((10#${nsecs_t2%%0}))))
	if (( $nsecs < 0 )); then
	    secs=$(($secs - 1))
	    nsecs=$((999999999 + $nsecs))
	fi
	local timediff=$(printf '%02d:%02d:%02d.%03d' $((10#$secs/3600)) $((10#$secs%3600/60)) $((10#$secs%60)) $((10#$nsecs/1000000)))
	echo "$timediff"
}

#extArg="-extended"
#passOn=${@#$extArg}

successCount=0
declare -a failures

function runTestScript
{
    local testName="$1"
    shift

    echo -e "=== Running testscript ${testName} [$*] ==="

    local time_start=$(get_time)
    if eval "$@"
    then
        successCount=$(expr $successCount + 1)
        local time_end=$(get_time)
        echo "--- Success: ${testName} --- | exectime: $(difftime $time_end $time_start)"
    else
        failures[${#failures[@]}]="$testName"
        echo "!!! FAIL: ${testName} !!!"
    fi

    echo
}

if [ "x${ENABLE_PASTELD}${ENABLE_UTILS}${ENABLE_WALLET}" != "x111" ]; then
  echo "No rpc tests to run. Wallet, utils, and pasteld must all be enabled"
  exit 0
fi

shopt -s extglob

testGroupName=
testScriptName=
testParams=
# parse command-line arguments
while (( "$#" )); do
  case "$1" in
    -h|--help)
      show_usage
      exit 0
      ;;
    --group=*) # test group name
      testGroupName=${1:8}
      shift
      ;;
    -g=*) # test group name
      testGroupName=${1:3}
      shift
      ;;
    --name=*) # test script name
      testScriptName=${1:7}
      shift
      ;;
    -n=*) # test script name
      testScriptName=${1:3}
      shift
      ;;
    *) # test script parameters
      if [[ $1  == *.py ]]; then
        echo "ERROR ! Use [$0 --name=testscript.py %parameters%] to execute single test script"
        exit 1
      fi
      testParams+=" ${1}"
      shift
      ;;
  esac
done

# get full script file path
# parameters:
#   $1 - script name (can be without .py extension)
function getScriptPath()
{
	local a=($1)
	local s=""
	for p in "${a[@]}"
	do
	    if test -z "$s"; then
		local scriptName=${p%.*}
		s="${BUILDDIR}/qa/rpc-tests/${scriptName}.py ${params}"
	    else
		s+=" $p" # add script options
	    fi
	done
	echo $s
}

# run group of tests
# parameters:
#   $1 - name of the array with test script names
function runTestGroup()
{
    local testGroupName=$1[@]
    eval scriptArray=( '"${!testGroupName}"' )
    len=${#scriptArray[@]}
    echo "Executing $len test scripts, group [$1]"
    for ScriptName in "${scriptArray[@]}"; do
      scriptFileName=$(getScriptPath "$ScriptName")
      runTestScript "$ScriptName" "$scriptFileName" --srcdir="${BUILDDIR}/src" $testParams
    done   
}

if test -n "$testGroupName"; then
    runTestGroup "$testGroupName"
elif test -n "$testScriptName"; then
    scriptFileName=$(getScriptPath "$testScriptName")
    runTestScript "$testScriptName" "$scriptFileName" --srcdir="${BUILDDIR}/src" $testParams
else
    runTestGroup "testScripts"
    runTestGroup "testScriptsExt"
    runTestGroup "testScriptsMN"
fi

echo -e "\n\nTests completed: $(expr $successCount + ${#failures[@]})"
echo "successes $successCount; failures: ${#failures[@]}"

if [ ${#failures[@]} -gt 0 ]
then
    echo -e "\nFailing tests: ${failures[*]}"
    exit 1
else
    exit 0
fi
