```shell
== Addressindex ==
getaddressmempool {addresses: [taddr, ...]}

== Blockchain ==
getbestblockhash
getblock "hash|height" ( verbosity )
getblockchaininfo
getblockcount
getblockdeltas blockhash
getblockhash index
getblockheader "hash" ( verbose )
getchaintips
getdifficulty
getmempoolinfo
getrawmempool ( verbose )
gettxout "txid" n ( includemempool )
gettxoutproof ["txid",...] ( blockhash )
gettxoutsetinfo
verifychain ( checklevel numblocks )
verifytxoutproof "proof"

== Control ==
getinfo
getmemoryinfo
help ( "command" )
stop

== Generating ==
generate numblocks
getgenerate
setgenerate generate ( genproclimit )

== Mining ==
getblocksubsidy height
getblocktemplate ( "jsonrequestobject" )
getlocalsolps
getmininginfo
getnetworkhashps ( blocks height )
getnetworksolps ( blocks height )
getnextblocksubsidy
prioritisetransaction <txid> <priority delta> <fee delta>
submitblock "hexdata" ( "jsonparametersobject" )

== Mnode ==
chaindata "command"...
getfeeschedule
"ingest" ingest|ani2psl|ani2psl_secret ...
masternode "command"...
masternodebroadcast "command"...
masternodelist ( "mode" "filter" )
mnsync [status|next|reset]
pastelid "command"...
storagefee "command"...
tickets "command"...

== Network ==
addnode "node" "add|remove|onetry"
clearbanned
disconnectnode "node"
getaddednodeinfo dns ( "node" )
getconnectioncount
getdeprecationinfo
getnettotals
getnetworkinfo
getpeerinfo
listbanned
ping
setban "ip(/netmask) "add|remove" (bantime) (absolute)

== Rawtransactions ==
createrawtransaction [{"txid":"id", "vout":n},...] {"address":amount,...} ( locktime ) ( expiryheight )
decoderawtransaction "hexstring"
decodescript "hex"
fundrawtransaction "hexstring"
getrawtransaction "txid" ( verbose "blockhash")
sendrawtransaction "hexstring" ( allowhighfees )
signrawtransaction "hexstring" ( [{"txid":"id","vout":n,"scriptPubKey":"hex","redeemScript":"hex"},...] ["privatekey1",...] sighashtype )

== Util ==
createmultisig nrequired ["key",...]
estimatefee nblocks
estimatepriority nblocks
validateaddress "t-address"
verifymessage "t-address" "signature" "message"
z_validateaddress "zaddr"

== Wallet ==
addmultisigaddress nrequired ["key",...] ( "account" )
backupwallet "destination"
dumpprivkey "t-addr"
dumpwallet "filename"
encryptwallet "passphrase"
fixmissingtxs <starting_height>
getaccount "zcashaddress"
getaccountaddress "account"
getaddressesbyaccount "account"
getbalance ( "account" minconf includeWatchonly )
getnewaddress ( "account" )
getrawchangeaddress
getreceivedbyaccount "account" ( minconf )
getreceivedbyaddress "zcashaddress" ( minconf )
gettransaction "txid" ( includeWatchonly )
gettxfee "txid"
getunconfirmedbalance
getwalletinfo
importaddress "address" ( "label" rescan )
importprivkey "zcashprivkey" ( "label" rescan )
importwallet "filename"
keypoolrefill ( newsize )
listaccounts ( minconf includeWatchonly)
listaddressamounts (includeEmpty ismineFilter)
listaddressgroupings
listlockunspent
listreceivedbyaccount ( minconf includeempty includeWatchonly)
listreceivedbyaddress ( minconf includeempty includeWatchonly)
listsinceblock ( "blockhash" target-confirmations includeWatchonly)
listtransactions ( "account" count from includeWatchonly)
listunspent ( minconf maxconf  ["address",...] )
lockunspent unlock [{"txid":"txid","vout":n},...]
move "fromaccount" "toaccount" amount ( minconf "comment" )
scanformissingtxs <starting_height>
sendfrom "fromaccount" "tozcashaddress" amount ( minconf "comment" "comment-to" )
sendmany "fromaccount" {"address":amount,...} ( minconf "comment" ["address",...] "change-address" )
sendtoaddress "t-address" amount ("comment" "comment-to" subtractFeeFromAmount "change-address")
setaccount "zcashaddress" "account"
settxfee amount
signmessage "t-addr" "message"
z_exportkey "zaddr"
z_exportviewingkey "zaddr"
z_exportwallet "filename"
z_getbalance "address" ( minconf )
z_getnewaddress(type)
z_getnotescount
z_getoperationresult (["operationid", ... ])
z_getoperationstatus (["operationid", ... ])
z_gettotalbalance ( minconf includeWatchonly )
z_importkey "zkey" ( rescan startHeight )
z_importviewingkey "vkey" ( rescan startHeight )
z_importwallet "filename"
z_listaddresses ( includeWatchonly )
z_listoperationids
z_listreceivedbyaddress "address" ( minconf )
z_listunspent ( minconf maxconf includeWatchonly ["zaddr",...] )
z_mergetoaddress ["fromaddress", ... ] "toaddress" ( fee ) ( transparent_limit ) ( shielded_limit ) ( memo )
z_sendmany "fromaddress" [{"address":... ,"amount":...},...] ( minconf ) ( fee )
z_sendmanywithchangetosender "fromaddress" [{"address":... ,"amount":...},...] ( minconf ) ( fee )
z_shieldcoinbase "fromaddress" "tozaddress" ( fee ) ( limit )
z_viewtransaction "txid"
zcbenchmark benchmarktype samplecount
```
