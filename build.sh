#!/bin/bash
# Usage:
#  Linux: ./build.sh
#  MingW: HOST=x86_64-w64-mingw32 ./build.sh
#  macOS: HOST=x86_64-apple-darwin14 ./build.sh
set -euo pipefail

function cmd_pref()
{
    if type -p "$2" > /dev/null; then
        eval "$1=$2"
    else
        eval "$1=$3"
    fi
}

# If a g-prefixed version of the command exists, use it preferentially.
function gprefix()
{
    cmd_pref "$1" "g$2" "$2"
}

function show_usage()
{
    cat <<EOF
Usage:

$0 --help
  Show this help message and exit.

$0 [ --enable-lcov || --disable-tests ] [ --disable-mining ] [ --enable-proton ] [ --enable-compress ] [ --enable-debug ] [ --enable-pvs ] [ MAKEARGS... ]
  Build Pastel and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Pastel itself.

  If --enable-lcov is passed, Pastel is configured to add coverage
  instrumentation, thus enabling "make cov" to work.
  If --disable-tests is passed instead, the Pastel tests are not built.

  If --disable-mining is passed, Pastel is configured to not build any mining
  code. It must be passed after the test arguments, if present.

  If --enable-proton is passed, Pastel is configured to build the Apache Qpid Proton
  library required for AMQP support. This library is not built by default.

  if --enable-compress is passed, Pastel is configured to compress ticket data with
  zstd library. This is enabled by default.

  if --enable-debug is passed, Pastel will be built with the debug options

  if --enable-pvs is passed, PVS Studio static code analyzer will be used
EOF
}

OS=`uname | cut -f1 -d_`
# get number of cpus
#NCPUS=`nproc`
NCPUS=4
#JOBCOUNT=$NCPUS

gprefix READLINK readlink
BUILDDIR=`dirname $($READLINK -f $0)`
export BUILDDIR
cd $BUILDDIR

# Allow user overrides to $MAKE. Typical usage for users who need it:
#   MAKE=gmake ./pcutil/build.sh -j$(nproc)
if test -z "${MAKE-}"; then
    MAKE=make
fi

# Allow overrides to $BUILD and $HOST for porters. Most users will not need it.
#   BUILD=i686-pc-linux-gnu ./build.sh
if test -z "${BUILD-}"; then
    BUILD="$(./depends/config.guess)"
fi
if test -z "${HOST-}"; then
    HOST="$BUILD"
fi
# Allow users to set arbitrary compile flags. Most users will not need this.
if test -z "${CONFIGURE_FLAGS-}"; then
    CONFIGURE_FLAGS=""
fi

# enable ELF-hardening option by default
bHardening=1
# disable proton library by default
bProton=0
# verbose compiler output
bVerbose=1
# enable ticket compression by default
bCompress=1
# debug mode
bDebugMode=0
build_mode="release"
# enable/disable PVS
bUsePVS=0

if [[ "$HOST" == *darwin* ]]; then
	bHardening=0
fi

# parse command-line parameters
PARAMS=
# positional arguments
POSARGS=

shopt -s extglob
while (( "$#" )); do
  case "$1" in
    --enable-lcov)
      PARAMS+=" $1"
      bHardening=0
      shift
      ;;
    --disable-tests)
      PARAMS+=" --enable-tests=no"
      shift
      ;;
    --disable-mining)
      PARAMS+=" --enable-mining=no"
      shift
      ;;
    --enable-proton)
      bProton=1
      PARAMS+=" $1"
      shift
      ;;
    --enable-compress)
      bCompress=1
      PARAMS+=" $1"
      shift
      ;;
    --enable-debug)
      PARAMS+=" $1"
#      if [[ "$HOST" == *mingw32* ]]; then
        PARAMS+=" CFLAGS=-ggdb"
#      else
#        PARAMS+=" --enable-debug=yes"
#      fi
#      POSARGS+=" DEBUG=1"
      bDebugMode=1
      build_mode="debug"
      shift
      ;;
    --enable-pvs)
      bUsePVS=1
      shift
      ;;
    -j+([[:digit:]]))
      JOBCOUNT=${1:2}
      shift
      ;;
    -j|--jobs)
      if [ -n "${2-}" ] && [ ${2:0:1} != "-" ]; then
        JOBCOUNT=$2
      else
        echo "Error: job count argument is missing" >&2
        exit 1
      fi
      shift 2
      ;;
    -h|--help)
      show_usage
      exit 0
      ;;
    V=0)
      bVerbose=0
      shift
      ;;
    V=1)
      bVerbose=1
      shift
      ;;
    -*|--*=) # unsupported flags
      echo "Error: unsupported option $1" >&2
      exit 1
      ;;
    *) # preserve positional arguments
      POSARGS+=" $1"
      shift
      ;;
  esac
done

if (( $JOBCOUNT > $NCPUS )); then
   echo "job count reduced to $NCPUS"
   JOBCOUNT=$NCPUS
else
   echo "job count: $JOBCOUNT"
fi
if (( $bHardening == 1 )); then
  PARAMS+=" --enable-hardening"
else
  PARAMS+=" --disable-hardening"
fi
if (( $bProton == 0 )); then
  PARAMS+=" --enable-proton=no"
  POSARGS+=" NO_PROTON=1"
fi
if (( $bCompress == 1 )); then
  PARAMS+=" --enable-compress=yes"
fi
POSARGS+=" V=$bVerbose"

echo "=============== Pastel Core ${build_mode} BUILD ==(use [-h] for help, pid=$$)==========="
echo "OS: $OS"
set -x
eval "$MAKE" --version
as --version
ld -v

if (( $bUsePVS == 1 )); then
    if ! command -v pvs-studio-analyzer &> /dev/null; then
    	echo "PVS Studio is not installed"
    	if command -v apt &> /dev/null
    	then
    		echo "Installing PVS Studio"
    		tmpInstallDir="$BUILDDIR/build-aux/tmp"
    		mkdir -p "$tmpInstallDir"
    		cd "$tmpInstallDir"
    		wget -q -O - https://files.viva64.com/etc/pubkey.txt | sudo apt-key add -
    		sudo wget -O /etc/apt/sources.list.d/viva64.list https://files.viva64.com/etc/viva64.list
    		sudo apt update
    		sudo apt install -y --no-install-recommends pvs-studio
    		cd "$BUILDDIR"
    		rm -rf "$tmpInstallDir"
    	fi
    fi
    echo "Using PVS Studio static code analyzer"
fi
    
echo PARAMS=$PARAMS; POSARGS=$POSARGS
# build Pastel dependent libraries in /depends folder
HOST="$HOST" BUILD="$BUILD" JOBCOUNT="$JOBCOUNT" \
  "$MAKE" -C ./depends/ --jobs=$JOBCOUNT $POSARGS
# use autoconf to generate 'configure' script from configure.ac
./autogen.sh
# configure Pastel
CONFIG_SITE="$PWD/depends/$HOST/share/config.site" \
    ./configure $PARAMS $CONFIGURE_FLAGS
# build Pastel executables
if (( $bUsePVS == 1 )); then
    pvs-studio-analyzer trace -- "$MAKE" --jobs=$JOBCOUNT $POSARGS
else
    "$MAKE" --jobs=$JOBCOUNT $POSARGS
fi
