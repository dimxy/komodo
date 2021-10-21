#!/bin/bash
export HOST=x86_64-w64-mingw32
CXX=x86_64-w64-mingw32-g++-posix
CC=x86_64-w64-mingw32-gcc-posix
PREFIX="$(pwd)/depends/$HOST"

set -eu -o pipefail

set -x
cd "$(dirname "$(readlink -f "$0")")/.."

# make dependences
cd depends/ && make HOST=$HOST V=1 NO_QT=1
cd ../

# note: cclib building now added to src/Makefile.am
#BUILD CCLIB
# WD=$PWD
# cd src/cc
# echo $PWD
# ./makecustom
# cd $WD

./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site CXXFLAGS="-DPTW32_STATIC_LIB -DCURL_STATICLIB -DCURVE_ALT_BN128 -fopenmp -pthread" ./configure --prefix="${PREFIX}" --host=x86_64-w64-mingw32 --enable-static --disable-shared \
  --with-custom-bin=yes CUSTOM_BIN_NAME=tokel CUSTOM_BRAND_NAME=Tokel \
  CUSTOM_SERVER_ARGS="'-ac_name=TOKEL -ac_supply=100000000 -ac_eras=2 -ac_cbmaturity=1 -ac_reward=100000000,4250000000 -ac_end=80640,0 -ac_decay=0,77700000 -ac_halving=0,525600 -ac_cc=555 -ac_ccenable=236,245,246,247 -ac_adaptivepow=6 -addnode=135.125.204.169 -addnode=192.99.71.125 -nspv_msg=1'" \
  CUSTOM_CLIENT_ARGS='-ac_name=TOKEL'
sed -i 's/-lboost_system-mt /-lboost_system-mt-s /' configure 
  
cd src/
# note: to build tokeld, tokel-cli it should not exist 'komodod.exe komodo-cli.exe' param here:
CC="${CC} -g " CXX="${CXX} -g " make V=1    
