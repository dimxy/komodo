#!/bin/bash
export CC=gcc-8
export CXX=g++-8
export LIBTOOL=libtool
export AR=ar
export RANLIB=ranlib
export STRIP=strip
export OTOOL=otool
export NM=nm

set -eu -o pipefail

# Allow users to set arbitrary compile flags. Most users will not need this.
if [[ -z "${CONFIGURE_FLAGS-}" ]]; then
    CONFIGURE_FLAGS=""
fi

if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:

$0 --help
  Show this help message and exit.

$0 [ --enable-lcov ] [ MAKEARGS... ]
  Build Zcash and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Zcash itself. If
  --enable-lcov is passed, Zcash is configured to add coverage
  instrumentation, thus enabling "make cov" to work.
EOF
    exit 0
fi

# If --enable-lcov is the first argument, enable lcov coverage support:
LCOV_ARG=''
HARDENING_ARG='--disable-hardening'
if [ "x${1:-}" = 'x--enable-lcov' ]
then
    LCOV_ARG='--enable-lcov'
    HARDENING_ARG='--disable-hardening'
    shift
fi

TRIPLET=`./depends/config.guess`
PREFIX="$(pwd)/depends/$TRIPLET"

make "$@" -C ./depends/ V=1 NO_QT=1 NO_PROTON=1

# cclib building now added to src/Makefile.am
#BUILD CCLIB
# WD=$PWD
# cd src/cc
# echo $PWD
# echo Making cclib...
# ./makecustom
# cd $WD

./autogen.sh
CPPFLAGS="-I$PREFIX/include -arch x86_64" LDFLAGS="-L$PREFIX/lib -arch x86_64 -Wl,-no_pie" \
CXXFLAGS="-arch x86_64 -I/usr/local/Cellar/gcc\@8/8.3.0/include/c++/8.3.0/ -fwrapv -fno-strict-aliasing -Wno-builtin-declaration-mismatch -Werror -Wno-error=attributes -g -Wl,-undefined -Wl,dynamic_lookup" \
./configure --prefix="${PREFIX}" --with-gui=no "$HARDENING_ARG" "$LCOV_ARG" "$CONFIGURE_FLAGS" \
  --with-custom-bin=yes CUSTOM_BIN_NAME=tokel CUSTOM_BRAND_NAME=Tokel \
  CUSTOM_SERVER_ARGS="'-ac_name=TOKEL -ac_supply=100000000 -ac_eras=2 -ac_cbmaturity=1 -ac_reward=100000000,4250000000 -ac_end=80640,0 -ac_decay=0,77700000 -ac_halving=0,525600 -ac_cc=555 -ac_ccenable=236,245,246,247 -ac_adaptivepow=6 -addnode=135.125.204.169 -addnode=192.99.71.125 -nspv_msg=1'" \
  CUSTOM_CLIENT_ARGS='-ac_name=TOKEL'

make "$@" V=1 NO_GTEST=1 STATIC=1 