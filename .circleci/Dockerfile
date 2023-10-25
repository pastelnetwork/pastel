FROM ubuntu:20.04

ENV TZ=America/New_York
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone \
  && apt update \
  && apt install -y --no-install-recommends \
     pkg-config tzdata make automake cmake libtool patch \
     bsdmainutils curl wget zip unzip lbzip2 xz-utils zlib1g-dev git openssh-client \
     g++-10-multilib mingw-w64 python3-dev python3-pip gnupg2\
  && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100 --slave \
     /usr/bin/g++ g++ /usr/bin/g++-10 --slave /usr/bin/gcov gcov /usr/bin/gcov-10 \
  && update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix \
  && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix \
  && ln -fs /usr/bin/x86_64-linux-gnu-gcc-10 /usr/bin/x86_64-linux-gnu-gcc \
  && ln -fs /usr/bin/gcc /usr/bin/cc && pip3 install pyblake2 simplejson
RUN apt clean

WORKDIR /pastel