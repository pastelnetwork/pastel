ACLOCAL_AMFLAGS = -I build-aux/m4
SUBDIRS = src
if ENABLE_MAN
SUBDIRS += doc/man
endif
.PHONY: deploy analyze test FORCE

GZIP_ENV="-9n"

if BUILD_PASTEL_LIBS
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libzcashconsensus.pc
endif

PASTELD_BIN=$(top_builddir)/src/$(PASTEL_DAEMON_NAME)$(EXEEXT)
PASTEL_CLI_BIN=$(top_builddir)/src/$(PASTEL_CLI_NAME)$(EXEEXT)

DIST_DOCS = $(wildcard doc/*.md) $(wildcard doc/release-notes/*.md)

BIN_CHECKS=$(top_srcdir)/contrib/devtools/symbol-check.py \
           $(top_srcdir)/contrib/devtools/security-check.py

COVERAGE_INFO = baseline_filtered_combined.info baseline.info \
  leveldb_baseline.info total_coverage.info \
  baseline_filtered.info leveldb_baseline_filtered.info  \
  pastel-gtest.info pastel-gtest_filtered.info pastel-gtest_coverage.info

dist-hook:
	-$(MAKE) -C $(top_distdir)/src/leveldb clean
	-$(MAKE) -C $(top_distdir)/src/secp256k1 distclean
	-$(GIT) archive --format=tar HEAD -- src/clientversion.cpp | $(AMTAR) -C $(top_distdir) -xf -

distcheck-hook:
	$(MKDIR_P) $(top_distdir)/_build/src/leveldb
	cp -rf $(top_srcdir)/src/leveldb/* $(top_distdir)/_build/src/leveldb/
	-$(MAKE) -C $(top_distdir)/_build/src/leveldb clean

distcleancheck:
	@:

$(if $(findstring src/,$(MAKECMDGOALS)),$(MAKECMDGOALS), none): FORCE
	$(MAKE) -C src $(patsubst src/%,%,$@)

$(PASTELD_BIN): FORCE
	$(MAKE) -C src $(@F)

$(PASTEL_CLI_BIN): FORCE
	$(MAKE) -C src $(@F)

if USE_LCOV

baseline.info:
	$(LCOV) -c -i -d $(abs_builddir)/src -o $@

baseline_filtered.info: baseline.info
	$(LCOV) -r $< "/usr/include/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/src/gtest/*" \
                      "$(abs_builddir)/src/wallet/gtest/*" \
                      -o $@

leveldb_baseline.info: baseline_filtered.info
	$(LCOV) -c -i -d $(abs_builddir)/src/leveldb -b $(abs_builddir)/src/leveldb -o $@

leveldb_baseline_filtered.info: leveldb_baseline.info
	$(LCOV) -r $< "/usr/include/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/src/gtest/*" \
                      "$(abs_builddir)/src/wallet/gtest/*" \
                      -o $@

baseline_filtered_combined.info: leveldb_baseline_filtered.info baseline_filtered.info
	$(LCOV) -a leveldb_baseline_filtered.info -a baseline_filtered.info -o $@

pastel-gtest.info: baseline_filtered_combined.info
	$(MAKE) -C src/ pastel-gtest_check
	$(LCOV) -c -d $(abs_builddir)/src -t pastel-gtest -o $@
	$(LCOV) -z -d $(abs_builddir)/src
	$(LCOV) -z -d $(abs_builddir)/src/leveldb

pastel-gtest_filtered.info: pastel-gtest.info
	$(LCOV) -r $< "/usr/include/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/src/gtest/*" \
                      "$(abs_builddir)/src/wallet/gtest/*" \
                      -o $@

pastel-gtest_coverage.info: baseline_filtered_combined.info pastel-gtest_filtered.info
	$(LCOV) -a baseline_filtered.info -a leveldb_baseline_filtered.info -a pastel-gtest_filtered.info -o $@

total_coverage.info:  baseline_filtered_combined.info pastel-gtest_filtered.info
	$(LCOV) -a baseline_filtered.info -a leveldb_baseline_filtered.info -a -a pastel-gtest_filtered.info -o $@ | $(GREP) "\%" | $(AWK) '{ print substr($$3,2,50) "/" $$5 }' > coverage_percent.txt

pastel-gtest.coverage/.dirstamp:  pastel-gtest_coverage.info
	$(GENHTML) -s $< -o $(@D)
	@touch $@

cov-pastel: pastel-gtest.coverage/.dirstamp

total.coverage/.dirstamp: total_coverage.info
	$(GENHTML) -s $< -o $(@D)
	@touch $@

endif

dist_bin_SCRIPTS = pcutil/fetch-params.sh
dist_noinst_SCRIPTS = autogen.sh pcutil/build-debian-package.sh build.sh

EXTRA_DIST = $(top_srcdir)/share/genbuild.sh qa/pull-tester/rpc-tests.sh qa/pull-tester/run-pastel-cli qa/rpc-tests qa/test-suite $(DIST_DOCS) $(BIN_CHECKS)

install-exec-hook:
	mv $(DESTDIR)$(bindir)/fetch-params.sh $(DESTDIR)$(bindir)/pastel-fetch-params

analyze:
	./pcutil/static_code_analysis.py

test:
	./qa/test-suite/full_test_suite.py $(filter-out $@,$(MAKECMDGOALS))

.INTERMEDIATE: $(COVERAGE_INFO)

DISTCHECK_CONFIGURE_FLAGS = --enable-man

clean-local:
	rm -rf pastel-gtest.coverage/ total.coverage/
