Pastel v1.1 - Cezanne

[![PastelNetwork](https://circleci.com/gh/pastelnetwork/pastel.svg?style=shield)](https://circleci.com/pastelnetwork/pastel)

What is Pastel?
--------------

#### :lock: Security Warnings

See important security warnings on the
[Pastel Security Information page](http://pastel.network/).

**Pastel is experimental and a work-in-progress.** Use at your own risk.

####  :ledger: Deprecation Policy

This release is considered deprecated 16 weeks after the release day. There
is an automatic deprecation shutdown feature which will halt the node some
time after this 16 week time period. The automatic feature is based on block
height.

## Getting Started

Building
--------

Build Pastel along with most dependencies from source by running
./pcutil/build.sh. Currently only Linux and Windows are officially supported.

### Dependencies
#### Main
```shell
sudo apt-get install \
build-essential pkg-config libc6-dev m4 g++-multilib \
autoconf libtool ncurses-dev unzip git python python-zmq \
zlib1g-dev wget curl bsdmainutils automake
```

#### To build for Windows (on Linux)
```shell
sudo apt-get install mingw-w64

## To display a current selection and make a new selection - !!! POSIX version must be selected !!!
sudo update-alternatives --config x86_64-w64-mingw32-gcc
sudo update-alternatives --config x86_64-w64-mingw32-g++
```

#### To run test suite
```shell
sudo apt-get install python-pip
sudo pip install pyblake2
```

### Build

#### Default build (for Ubuntu on Ubuntu) 
```shell
./pcutil/build.sh -j$(nproc)
```

> For build without build-in CPU miner use
```shell
./pcutil/build.sh -j$(nproc) --disable-mining
```

#### Cross-platforms builds on Ubuntu
##### For Windows
```shell
HOST=x86_64-w64-mingw32 ./pcutil/build.sh -j$(nproc)
```
> Windows build must be done on the clean tree:
```shell
make clean && make -C src/univalue clean
```

##### For Mac OSX
```shell
HOST=x86_64-apple-darwin ./pcutil/build.sh -j$(nproc)
```
> Apple build must be done on the clean tree:
```shell
make clean && make -C src/univalue clean
```

License
-------

For license information see the file [COPYING](COPYING).
