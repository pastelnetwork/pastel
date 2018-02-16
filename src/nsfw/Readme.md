This project is client side for NSFW (Not Suitable For Work) image detection. This performs as asynchronous
task queue where new tasks simultaneously are pushed in queue.

Dependencies:
Boost library. Currently used version is Boost 1.66.0 (as listed in CMakeList.txt). Make sure that this
library is available on path specified in CMakeList.txt.

Build:
Build can be done with cmake higher than 3.8 (can be downloaded here https://cmake.org/download/). Command for build
"cmake ."
This command generates also Makefile.
You can also use CLion for build the target (open project and then build)