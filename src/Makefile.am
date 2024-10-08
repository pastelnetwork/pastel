DIST_SUBDIRS = secp256k1 univalue

AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) $(SAN_LDFLAGS) $(HARDENED_LDFLAGS)
AM_CXXFLAGS = $(SAN_CXXFLAGS) $(HARDENED_CXXFLAGS) $(ERROR_CXXFLAGS)
AM_CPPFLAGS = $(HARDENED_CPPFLAGS)
EXTRA_LIBRARIES =

if EMBEDDED_LEVELDB
LEVELDB_CPPFLAGS += -I$(srcdir)/leveldb/include
LEVELDB_CPPFLAGS += -I$(srcdir)/leveldb/helpers/memenv
LIBLEVELDB += $(builddir)/leveldb/libleveldb.a
LIBMEMENV  += $(builddir)/leveldb/libmemenv.a

# NOTE: This dependency is not strictly necessary, but without it make may try to build both in parallel, which breaks the LevelDB build system in a race
$(LIBLEVELDB): $(LIBMEMENV)

$(LIBLEVELDB) $(LIBMEMENV):
	@echo "Building LevelDB ..." && $(MAKE) -C $(@D) $(@F) CXX="$(CXX)" \
	  CC="$(CC)" PLATFORM=$(TARGET_OS) AR="$(AR)" $(LEVELDB_TARGET_FLAGS) \
          OPT="$(AM_CXXFLAGS) $(PIE_FLAGS) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) -D__STDC_LIMIT_MACROS"
endif

PASTEL_CONFIG_INCLUDES=-I$(builddir)/config
PASTEL_INCLUDES=-I$(builddir) -I$(builddir)/obj $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)

PASTEL_INCLUDES += -I$(srcdir)/secp256k1/include
PASTEL_INCLUDES += -I$(srcdir)/snark
PASTEL_INCLUDES += -I$(srcdir)/snark/libsnark
PASTEL_INCLUDES += -I$(srcdir)/univalue/include
PASTEL_INCLUDES += -I$(srcdir)/msgpack

LIBBITCOIN_SERVER=libbitcoin_server.a
LIBBITCOIN_COMMON=libbitcoin_common.a
LIBBITCOIN_CLI=libbitcoin_cli.a
LIBBITCOIN_UTIL=libbitcoin_util.a
LIBBITCOIN_CRYPTO=crypto/libbitcoin_crypto.a
LIBSECP256K1=secp256k1/libsecp256k1.la
LIBSNARK=snark/libsnark.a
LIBUNIVALUE=univalue/libunivalue.la
LIBZCASH=libzcash.a

if ENABLE_ZMQ
LIBBITCOIN_ZMQ=libbitcoin_zmq.a
endif
if ENABLE_PROTON
LIBBITCOIN_PROTON=libbitcoin_proton.a
endif
if BUILD_PASTEL_LIBS
LIBZCASH_CONSENSUS=libzcashconsensus.la
endif
if ENABLE_WALLET
LIBBITCOIN_WALLET=libbitcoin_wallet.a
endif

$(LIBSECP256K1): $(wildcard secp256k1/src/*) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

LIBUNIVALUE_CXXFLAGS = $(CXXFLAGS) $(AM_CXXFLAGS) $(PIC_FLAGS)

LIBSNARK_CXXFLAGS = $(CXXFLAGS) $(AM_CXXFLAGS) $(PIC_FLAGS) -DBINARY_OUTPUT -DNO_PT_COMPRESSION=1 -fstack-protector-all
LIBSNARK_CONFIG_FLAGS = CURVE=ALT_BN128 NO_PROCPS=1 NO_DOCS=1 STATIC=1 NO_SUPERCOP=1 FEATUREFLAGS=-DMONTGOMERY_OUTPUT NO_COPY_DEPINST=1 NO_COMPILE_LIBGTEST=1
if HAVE_OPENMP
LIBSNARK_CONFIG_FLAGS += MULTICORE=1
endif
if TARGET_DARWIN
LIBSNARK_CONFIG_FLAGS += PLATFORM=darwin
endif

$(LIBSNARK): $(wildcard snark/src/*)
	$(AM_V_at) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" CXXFLAGS="$(LIBSNARK_CXXFLAGS)" $(MAKE) $(AM_MAKEFLAGS) -C snark/ DEPINST="$(LIBSNARK_DEPINST)" $(LIBSNARK_CONFIG_FLAGS) OPTFLAGS="-O2 -march=x86-64"

libsnark-tests: $(wildcard snark/src/*)
	$(AM_V_at) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" CXXFLAGS="$(LIBSNARK_CXXFLAGS)" $(MAKE) $(AM_MAKEFLAGS) -C snark/ check DEPINST="$(LIBSNARK_DEPINST)" $(LIBSNARK_CONFIG_FLAGS) OPTFLAGS="-O2 -march=x86-64"

$(LIBUNIVALUE): $(wildcard univalue/lib/*) $(wildcard univalue/include/*)
	$(AM_V_at) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" CXXFLAGS="$(LIBUNIVALUE_CXXFLAGS)" $(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES += \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_CLI) \
  $(LIBBITCOIN_WALLET) \
  $(LIBBITCOIN_ZMQ) \
  $(LIBBITCOIN_PROTON) \
  $(LIBZCASH)

lib_LTLIBRARIES = $(LIBZCASH_CONSENSUS)

bin_PROGRAMS =
noinst_PROGRAMS =
TESTS =

if BUILD_PASTELD
  bin_PROGRAMS += pasteld
endif

if BUILD_PASTEL_UTILS
  bin_PROGRAMS += pastel-cli pastel-tx
endif

LIBZCASH_H = \
  zcash/IncrementalMerkleTree.hpp \
  zcash/NoteEncryption.hpp \
  zcash/Address.hpp \
  zcash/address/sapling.hpp \
  zcash/address/zip32.h \
  zcash/Note.hpp \
  zcash/prf.h \
  zcash/Proof.hpp \
  zcash/util.h \
  zcash/Zcash.h

MNODE_CPP =\
  mnode/rpc/generate-report.cpp \
  mnode/rpc/governance-rpc.cpp \
  mnode/rpc/ingest.cpp \
  mnode/rpc/masternode.cpp \
  mnode/rpc/masternodebroadcast.cpp \
  mnode/rpc/mnode-rpc-utils.cpp \
  mnode/rpc/mnode-rpc.cpp \
  mnode/rpc/pastelid-rpc.cpp \
  mnode/rpc/rpt-fees-burn.cpp \
  mnode/rpc/storage-fee.cpp \
  mnode/rpc/tickets-activate.cpp \
  mnode/rpc/tickets-fake.cpp \
  mnode/rpc/tickets-list.cpp \
  mnode/rpc/tickets-register.cpp \
  mnode/rpc/tickets-find.cpp \
  mnode/rpc/tickets-findbylabel.cpp \
  mnode/rpc/tickets-get.cpp \
  mnode/rpc/tickets-tools.cpp \
  mnode/tickets/accept.cpp \
  mnode/tickets/action-reg.cpp \
  mnode/tickets/action-act.cpp \
  mnode/tickets/ethereum-address-change.cpp \
  mnode/tickets/nft-act.cpp\
  mnode/tickets/collection-act.cpp \
  mnode/tickets/collection-reg.cpp \
  mnode/tickets/collection-item.cpp \
  mnode/tickets/contract.cpp \
  mnode/tickets/nft-reg.cpp \
  mnode/tickets/nft-royalty.cpp \
  mnode/tickets/take-down.cpp \
  mnode/tickets/offer.cpp \
  mnode/tickets/pastelid-reg.cpp \
  mnode/tickets/ticket-extra-fees.cpp \
  mnode/tickets/ticket_signing.cpp \
  mnode/tickets/transfer.cpp \
  mnode/tickets/username-change.cpp \
  mnode/tickets/ticket-key.cpp \
  mnode/mnode-config.cpp \
  mnode/mnode-active.cpp \
  mnode/mnode-manager.cpp \
  mnode/mnode-masternode.cpp \
  mnode/mnode-msgsigner.cpp \
  mnode/mnode-requesttracker.cpp \
  mnode/mnode-sync.cpp \
  mnode/mnode-validation.cpp \
  mnode/mnode-payments.cpp \
  mnode/mnode-governance.cpp \
  mnode/mnode-messageproc.cpp \
  mnode/mnode-perfcheck.cpp \
  mnode/ticket-processor.cpp \
  mnode/p2fms-txbuilder.cpp \
  mnode/ticket-mempool-processor.cpp \
  mnode/ticket-txmempool.cpp \
  mnode/mnode-notificationinterface.cpp \
  mnode/mnode-controller.cpp

MNODE_H = \
  mnode/rpc/generate-report.h \
  mnode/rpc/governance-rpc.h \
  mnode/rpc/ingest.h \
  mnode/rpc/masternode.h \
  mnode/rpc/masternodebroadcast.h \
  mnode/rpc/mnode-rpc-utils.h \
  mnode/rpc/mnode-register.h \
  mnode/rpc/mnode-rpc.h \
  mnode/rpc/pastelid-rpc.h \
  mnode/rpc/rpt-fees-burn.h \
  mnode/rpc/storage-fee.h \
  mnode/rpc/tickets-activate.h \
  mnode/rpc/tickets-fake.h \
  mnode/rpc/tickets-list.h \
  mnode/rpc/tickets-register.h \
  mnode/rpc/tickets-find.h \
  mnode/rpc/tickets-findbylabel.h \
  mnode/rpc/tickets-get.h \
  mnode/rpc/tickets-tools.h \
  mnode/tickets/action-reg.h \
  mnode/tickets/action-act.h \
  mnode/tickets/ethereum-address-change.h \
  mnode/tickets/accept.h \
  mnode/tickets/ticket.h \
  mnode/tickets/ticket-extra-fees.h \
  mnode/tickets/ticket-mn-fees.h \
  mnode/tickets/ticket-types.h \
  mnode/tickets/ticket-utils.h \
  mnode/tickets/ticket_signing.h \
  mnode/tickets/tickets-all.h \
  mnode/tickets/nft-act.h \
  mnode/tickets/contract.h \
  mnode/tickets/collection-act.h \
  mnode/tickets/collection-reg.h \
  mnode/tickets/collection-item.h \
  mnode/tickets/nft-reg.h \
  mnode/tickets/nft-royalty.h \
  mnode/tickets/take-down.h \
  mnode/tickets/offer.h \
  mnode/tickets/pastelid-reg.h \
  mnode/tickets/transfer.h \
  mnode/tickets/username-change.h \
  mnode/tickets/ticket-key.h \
  mnode/mnode-active.h \
  mnode/mnode-config.h \
  mnode/mnode-db.h \
  mnode/mnode-manager.h \
  mnode/mnode-masternode.h \
  mnode/mnode-msgsigner.h \
  mnode/mnode-requesttracker.h \
  mnode/mnode-validation.h \
  mnode/mnode-payments.h \
  mnode/mnode-governance.h \
  mnode/mnode-messageproc.h \
  mnode/mnode-perfcheck.h \
  mnode/ticket-processor.h \
  mnode/p2fms-txbuilder.h \
  mnode/ticket-mempool-processor.h \
  mnode/ticket-txmempool.h \
  mnode/mnode-controller.h \
  mnode/mnode-notificationinterface.h \
  mnode/mnode-sync.h \
  mnode/mnode-consts.h \
  mnode/mnode-badwords.h

PASTELID_CPP =\
  pastelid/secure_container.cpp \
  pastelid/pastel_key.cpp

PASTELID_H = \
  pastelid/secure_container.h \
  pastelid/pastel_key.h \
  pastelid/common.h \
  pastelid/ed.h

UTILS_CPP = \
  utils/arith_uint256.cpp \
  utils/ascii85.cpp \
  utils/base58.cpp \
  utils/bech32.cpp \
  utils/datacompressor.cpp \
  utils/hash.cpp \
  utils/logmanager.cpp \
  utils/ping_util.cpp \
  utils/random.cpp \
  utils/scheduler.cpp \
  utils/sync.cpp \
  utils/trimmean.cpp \
  utils/uint256.cpp \
  utils/util.cpp \
  utils/utilstrencodings.cpp \
  utils/utiltime.cpp

UTILS_H = \
  utils/arith_uint256.h \
  utils/ascii85.h \
  utils/base58.h \
  utils/bech32.h \
  utils/datacompressor.h \
  utils/detect_cpp_standard.h \
  utils/enum_util.h \
  utils/fs.h \
  utils/hash.h \
  utils/logmanager.h \
  utils/map_types.h \
  utils/numeric_range.h \
  utils/ping_util.h \
  utils/prevector.h \
  utils/random.h \
  utils/reverselock.h \
  utils/scheduler.h \
  utils/serialize.h \
  utils/set_types.h \
  utils/str_types.h \
  utils/str_utils.h \
  utils/streams.h \
  utils/svc_thread.h \
  utils/sync.h \
  utils/threadsafety.h \
  utils/timer.h \
  utils/tinyformat.h \
  utils/trimmean.h \
  utils/uint256.h \
  utils/uint252.h \
  utils/util.h \
  utils/utilstrencodings.h \
  utils/utiltime.h \
  utils/vector_types.h


.PHONY: FORCE collate-libsnark check-symbols check-security
# pastel core #
PASTEL_CORE_H = \
  accept_to_mempool.h \
  addrman.h \
  alert.h \
  amount.h \
  amqp/amqpabstractnotifier.h \
  amqp/amqpconfig.h \
  amqp/amqpnotificationinterface.h \
  amqp/amqppublishnotifier.h \
  amqp/amqpsender.h \
  asyncrpcoperation.h \
  asyncrpcqueue.h \
  blockscanner.h \
  chain.h \
  chain_options.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  checkpoints.h \
  checkqueue.h \
  clientversion.h \
  coincontrol.h \
  coins.h \
  compat.h \
  compat/byteswap.h \
  compat/endian.h \
  compat/sanity.h \
  compressor.h \
  config/port_config.h \
  consensus/consensus.h \
  consensus/params.h \
  consensus/upgrades.h \
  consensus/validation.h \
  core_io.h \
  core_memusage.h \
  txdb/addressindex.h \
  txdb/fundstransferindex.h \
  txdb/index_defs.h \
  txdb/spentindex.h \
  txdb/txidxprocessor.h \
  txdb/timestampindex.h \
  txdb/txdb.h \
  deprecation.h \
  ecc_context.h \
  experimental_features.h \
  httprpc.h \
  httpserver.h \
  init.h \
  key.h \
  key_constants.h \
  key_io.h \
  keystore.h \
  dbwrapper.h \
  legroast.h \
  limitedmap.h \
  main.h \
  memusage.h \
  merkleblock.h \
  metrics.h \
  mining/eligibility-mgr.h \
  mining/miner.h \
  mining/mining-settings.h \
  mining/pow.h \
  mruset.h \
  netmsg/bloom.h \
  netmsg/block-cache.h \
  netmsg/fork-switch-tracker.h \
  netmsg/netconsts.h \
  netmsg/netmessage.h \
  netmsg/node.h \
  netmsg/nodestate.h \
  netmsg/nodemanager.h \
  net.h \
  net_manager.h \
  netbase.h \
  noui.h \
  orphan-tx.h\
  policy/fees.h \
  primitives/block.h \
  primitives/transaction.h \
  protocol.h \
  pubkey.h \
  rpc/client.h \
  rpc/chain-rpc-utils.h \
  rpc/coin-supply.h \
  rpc/protocol.h \
  rpc/server.h \
  rpc/register.h \
  rpc/rpc_consts.h \
  rpc/rpc_parser.h \
  rpc/rpc-utils.h \
  script_check.h \
  script/interpreter.h \
  script/script.h \
  script/scripttype.h \
  script/script_error.h \
  script/sigcache.h \
  script/sign.h \
  script/standard.h \
  support/allocators/secure.h \
  support/allocators/zeroafterfree.h \
  support/cleanse.h \
  support/events.h \
  support/lockedpool.h \
  timedata.h \
  torcontrol.h \
  transaction_builder.h \
  txmempool.h \
  txmempool_entry.h\
  ui_interface.h \
  undo.h \
  utilmoneystr.h \
  utiltest.h \
  validationinterface.h \
  version.h \
  wallet/asyncrpcoperation_mergetoaddress.h \
  wallet/asyncrpcoperation_sendmany.h \
  wallet/asyncrpcoperation_shieldcoinbase.h \
  wallet/crypter.h \
  wallet/db.h \
  wallet/missing_txs.h \
  wallet/rpcwallet.h \
  wallet/wallet.h \
  wallet/wallet_ismine.h \
  wallet/walletdb.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqconfig.h\
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h \
  $(MNODE_H) \
  $(PASTELID_H)

obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/share/genbuild.sh $(abs_top_builddir)/src/obj/build.h \
	  $(abs_top_srcdir)
libbitcoin_util_a-clientversion.$(OBJEXT): obj/build.h

# server: pasteld
libbitcoin_server_a_CPPFLAGS = $(AM_CPPFLAGS) $(PASTEL_INCLUDES) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libbitcoin_server_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_server_a_SOURCES = \
  sendalert.cpp \
  accept_to_mempool.cpp \
  addrman.cpp \
  alert.cpp \
  alertkeys.h \
  asyncrpcoperation.cpp \
  asyncrpcqueue.cpp \
  blockscanner.cpp \
  chain.cpp \
  chain_options.cpp \
  checkpoints.cpp \
  deprecation.cpp \
  experimental_features.cpp \
  httprpc.cpp \
  httpserver.cpp \
  init.cpp \
  dbwrapper.cpp \
  main.cpp \
  merkleblock.cpp \
  metrics.cpp \
  mining/eligibility-mgr.cpp \
  mining/miner.cpp \
  mining/mining-settings.cpp \
  mining/pow.cpp \
  netmsg/bloom.cpp \
  netmsg/block-cache.cpp \
  netmsg/fork-switch-tracker.cpp \
  netmsg/netmessage.cpp \
  netmsg/node.cpp \
  netmsg/nodestate.cpp \
  netmsg/nodemanager.cpp \
  net.cpp \
  net_manager.cpp \
  noui.cpp \
  orphan-tx.cpp \
  policy/fees.cpp \
  rest.cpp \
  rpc/blockchain.cpp \
  rpc/chain-rpc-utils.cpp \
  rpc/coin-supply.cpp \
  rpc/mining.cpp \
  rpc/misc.cpp \
  rpc/net.cpp \
  rpc/rawtransaction.cpp \
  rpc/server.cpp \
  script/sigcache.cpp \
  script_check.cpp \
  timedata.cpp \
  torcontrol.cpp \
  txdb/txdb.cpp \
  txdb/txidxprocessor.cpp \
  txmempool.cpp \
  validationinterface.cpp \
  $(MNODE_CPP) \
  $(PASTELID_CPP) \
  $(PASTEL_CORE_H) \
  $(LIBZCASH_H)


if ENABLE_ZMQ
libbitcoin_zmq_a_CPPFLAGS = $(PASTEL_INCLUDES) $(ZMQ_CFLAGS)
libbitcoin_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_zmq_a_SOURCES = \
  zmq/zmqabstractnotifier.cpp \
  zmq/zmqnotificationinterface.cpp \
  zmq/zmqpublishnotifier.cpp
endif

if ENABLE_PROTON
libbitcoin_proton_a_CPPFLAGS = $(PASTEL_INCLUDES)
libbitcoin_proton_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_proton_a_SOURCES = \
  amqp/amqpabstractnotifier.cpp \
  amqp/amqpnotificationinterface.cpp \
  amqp/amqppublishnotifier.cpp
endif

# wallet: pasteld, but only linked when wallet enabled
libbitcoin_wallet_a_CPPFLAGS = $(AM_CPPFLAGS) $(PASTEL_INCLUDES)
libbitcoin_wallet_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) -DENABLE_WALLET
libbitcoin_wallet_a_SOURCES = \
  zcbenchmarks.cpp \
  zcbenchmarks.h \
  wallet/asyncrpcoperation_mergetoaddress.cpp \
  wallet/asyncrpcoperation_sendmany.cpp \
  wallet/asyncrpcoperation_shieldcoinbase.cpp \
  wallet/crypter.cpp \
  wallet/db.cpp \
  wallet/missing_txs.cpp \
  wallet/rpcdump.cpp \
  wallet/rpcwallet.cpp \
  wallet/wallet.cpp \
  wallet/wallet_ismine.cpp \
  wallet/walletdb.cpp \
  $(PASTEL_CORE_H) \
  $(LIBZCASH_H)

# crypto primitives library
crypto_libbitcoin_crypto_a_CPPFLAGS = $(AM_CPPFLAGS) $(PASTEL_CONFIG_INCLUDES)
crypto_libbitcoin_crypto_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libbitcoin_crypto_a_SOURCES = \
  crypto/common.h \
  crypto/equihash.cpp \
  crypto/equihash.h \
  crypto/equihash.tcc \
  crypto/hmac_sha256.cpp \
  crypto/hmac_sha256.h \
  crypto/hmac_sha512.cpp \
  crypto/hmac_sha512.h \
  crypto/ripemd160.cpp \
  crypto/ripemd160.h \
  crypto/sha1.cpp \
  crypto/sha1.h \
  crypto/sha256.cpp \
  crypto/sha256.h \
  crypto/sha512.cpp \
  crypto/sha512.h

if ENABLE_MINING
EQUIHASH_TROMP_SOURCES = \
  mining/pow/tromp/equi_miner.h \
  mining/pow/tromp/equi.h \
  mining/pow/tromp/osx_barrier.h

crypto_libbitcoin_crypto_a_CPPFLAGS += -DEQUIHASH_TROMP_ATOMIC
crypto_libbitcoin_crypto_a_SOURCES += ${EQUIHASH_TROMP_SOURCES}
endif

# common: shared between pasteld and non-server tools
libbitcoin_common_a_CPPFLAGS = $(AM_CPPFLAGS) $(PASTEL_INCLUDES)
libbitcoin_common_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_common_a_SOURCES = \
  amount.cpp \
  chainparams.cpp \
  coins.cpp \
  compressor.cpp \
  consensus/upgrades.cpp \
  core_read.cpp \
  core_write.cpp \
  ecc_context.cpp\
  key.cpp \
  key_io.cpp \
  keystore.cpp \
  netbase.cpp \
  primitives/block.cpp \
  primitives/transaction.cpp \
  protocol.cpp \
  pubkey.cpp \
  script/interpreter.cpp \
  script/script.cpp \
  script/script_error.cpp \
  script/sign.cpp \
  script/standard.cpp \
  transaction_builder.cpp \
  utiltest.cpp \
  $(UTILS_H) \
  $(PASTEL_CORE_H) \
  $(LIBZCASH_H)

# util: shared between all executables.
# This library *must* be included to make sure that the glibc
# backward-compatibility objects and their sanity checks are linked.
libbitcoin_util_a_CPPFLAGS = $(AM_CPPFLAGS) $(PASTEL_INCLUDES)
libbitcoin_util_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_util_a_SOURCES =\
  compat/glibc_sanity.cpp \
  compat/glibcxx_sanity.cpp \
  compat/strnlen.cpp \
  support/lockedpool.cpp \
  support/cleanse.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  rpc/protocol.cpp \
  rpc/rpc-utils.cpp \
  utilmoneystr.cpp \
  $(UTILS_CPP) \
  $(UTILS_H) \
  $(PASTEL_CORE_H) \
  $(LIBZCASH_H)

if GLIBC_BACK_COMPAT
libbitcoin_util_a_SOURCES += compat/glibc_compat.cpp
endif

# cli: pastel-cli
libbitcoin_cli_a_CPPFLAGS = $(AM_CPPFLAGS) $(PASTEL_INCLUDES)
libbitcoin_cli_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_cli_a_SOURCES = \
  rpc/client.cpp \
  $(PASTEL_CORE_H) \
  $(LIBZCASH_H)

nodist_libbitcoin_util_a_SOURCES = $(srcdir)/obj/build.h
#

# pasteld binary #
pasteld_SOURCES = pasteld.cpp
pasteld_CPPFLAGS = $(AM_CPPFLAGS) $(PASTEL_INCLUDES)
pasteld_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
pasteld_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
pasteld_SOURCES += pasteld-res.rc
endif

pasteld_LDADD =\
  $(LIBBITCOIN_SERVER)\
  $(LIBBITCOIN_WALLET)\
  $(LIBBITCOIN_COMMON)\
  $(LIBUNIVALUE)\
  $(LIBBITCOIN_UTIL)\
  $(LIBBITCOIN_ZMQ)\
  $(LIBBITCOIN_PROTON)\
  $(LIBBITCOIN_CRYPTO)\
  $(LIBZCASH)\
  $(LIBSNARK)\
  $(LIBLEVELDB)\
  $(LIBMEMENV)\
  $(LIBSECP256K1)

pasteld_LDADD +=\
  $(BOOST_LIBS)\
  $(BDB_LIBS)\
  $(SSL_LIBS)\
  $(CRYPTO_LIBS)\
  $(EVENT_PTHREADS_LIBS)\
  $(EVENT_LIBS)\
  $(ZMQ_LIBS)\
  $(PROTON_LIBS)\
  $(LIBBITCOIN_CRYPTO)\
  $(LIBZCASH_LIBS)\
  $(ZSTD_LIBS)

# pastel-cli binary #
pastel_cli_SOURCES = pastel-cli.cpp
pastel_cli_CPPFLAGS = $(AM_CPPFLAGS) $(PASTEL_INCLUDES) $(EVENT_CFLAGS)
pastel_cli_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
pastel_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
pastel_cli_SOURCES += pastel-cli-res.rc
endif

pastel_cli_LDADD =\
  $(LIBBITCOIN_CLI)\
  $(LIBUNIVALUE)\
  $(LIBBITCOIN_UTIL)\
  $(BOOST_LIBS)\
  $(SSL_LIBS)\
  $(CRYPTO_LIBS)\
  $(EVENT_LIBS)\
  $(LIBZCASH)\
  $(LIBSNARK)\
  $(LIBBITCOIN_CRYPTO)\
  $(LIBZCASH_LIBS)
#

# pastel-tx binary #
pastel_tx_SOURCES = bitcoin-tx.cpp
pastel_tx_CPPFLAGS = $(AM_CPPFLAGS) $(PASTEL_INCLUDES)
pastel_tx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
pastel_tx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
pastel_tx_SOURCES += bitcoin-tx-res.rc
endif

# FIXME: Is libzcash needed for pastel_tx?
pastel_tx_LDADD =\
  $(LIBUNIVALUE)\
  $(LIBBITCOIN_COMMON)\
  $(LIBBITCOIN_UTIL)\
  $(LIBSECP256K1)\
  $(LIBZCASH)\
  $(LIBSNARK)\
  $(LIBBITCOIN_CRYPTO)\
  $(LIBZCASH_LIBS)

pastel_tx_LDADD += \
  $(BOOST_LIBS)\
  $(CRYPTO_LIBS)\
  $(ZSTD_LIBS)

# zcash protocol primitives #
libzcash_a_SOURCES = \
  zcash/IncrementalMerkleTree.cpp \
  zcash/NoteEncryption.cpp \
  zcash/Address.cpp \
  zcash/address/sapling.cpp \
  zcash/address/zip32.cpp \
  zcash/Proof.cpp \
  zcash/Note.cpp \
  zcash/prf.cpp \
  zcash/util.cpp \
  zcash/circuit/commitment.tcc \
  zcash/circuit/gadget.tcc \
  zcash/circuit/merkle.tcc \
  zcash/circuit/note.tcc \
  zcash/circuit/prfs.tcc \
  zcash/circuit/utils.tcc

libzcash_a_CPPFLAGS = $(AM_CPPFLAGS) $(PIC_FLAGS) -DMONTGOMERY_OUTPUT -DBINARY_OUTPUT -DCURVE_ALT_BN128 -DBOOST_SPIRIT_THREADSAFE -fvisibility=hidden -DSTATIC $(PASTEL_INCLUDES)
libzcash_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libzcash_a_LDFLAGS = $(AM_LDFLAGS)
noinst_LIBRARIES = libzcash.a

# zcashconsensus library #
if BUILD_PASTEL_LIBS
include_HEADERS = script/zcashconsensus.h
libzcashconsensus_la_SOURCES = \
  crypto/equihash.cpp \
  crypto/hmac_sha512.cpp \
  crypto/ripemd160.cpp \
  crypto/sha1.cpp \
  crypto/sha256.cpp \
  crypto/sha512.cpp \
  primitives/transaction.cpp \
  pubkey.cpp \
  script/zcashconsensus.cpp \
  script/interpreter.cpp \
  script/script.cpp \
  uint256.cpp \
  ascii85.cpp \
  utilstrencodings.cpp

if GLIBC_BACK_COMPAT
  libzcashconsensus_la_SOURCES += compat/glibc_compat.cpp
endif

libzcashconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libzcashconsensus_la_LIBADD = $(LIBSECP256K1)
libzcashconsensus_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj -I$(srcdir)/secp256k1/include -DBUILD_BITCOIN_INTERNAL
libzcashconsensus_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

endif
#

CLEANFILES=\
 leveldb/libleveldb.a \
 leveldb/libmemenv.a \
 *.gcda *.gcno */*.gcno wallet/*/*.gcno

DISTCLEANFILES = obj/build.h

EXTRA_DIST = leveldb snark

clean-local:
	-$(MAKE) -C leveldb clean
	-$(MAKE) -C secp256k1 clean
	-$(MAKE) -C snark clean
	-$(MAKE) -C univalue clean
	@for dir in $(wildcard ./deps/*/); do \
		if [ -f $$dir/Makefile ]; then \
			$(MAKE) -C $$dir clean; \
		fi; \
        done
	-rm -f leveldb/*/*.gcno leveldb/helpers/memenv/*.gcno
	-rm -f config.h

.rc.o:
	@test -f $(WINDRES)
	$(AM_V_GEN) $(WINDRES) -DWINDRES_PREPROC -I. -i $< -o $@

.mm.o:
	$(AM_V_CXX) $(OBJCXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(AM_CXXFLAGS) $(PIE_FLAGS) $(CXXFLAGS) -c -o $@ $<

check-symbols: $(bin_PROGRAMS)
if GLIBC_BACK_COMPAT
	@echo "Checking glibc back compat of [$(bin_PROGRAMS)]..."
	$(AM_V_at) READELF=$(READELF) CPPFILT=$(CPPFILT) $(top_srcdir)/contrib/devtools/symbol-check.py $(bin_PROGRAMS)
endif

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security of [$(bin_PROGRAMS)]..."
	$(AM_V_at) READELF=$(READELF) OBJDUMP=$(OBJDUMP) $(top_srcdir)/contrib/devtools/security-check.py $(bin_PROGRAMS)
endif

%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(abspath $(<D) $<)

if ENABLE_TESTS
include Makefile.gtest.include
endif
