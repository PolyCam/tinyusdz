#
# Build Python 3.10.6 and install it to ci/dist/python so that we don't need separated Python installation(which is sometimes difficult to setup on C.I. environment)
#
# This build is minimal and disables many features, including ZLIB and OpenSSL support, assuming TinyUSDZ python module does not require such python modules(ZLIB, SSL, MD5, SHA256, ...) 
#
git clone https://github.com/lighttransport/python-cmake-buildsystem ci/python-cmake-buildsystem

# It seems `python` binary will be built by symlinking libpython.so when `BUILD_LIBPYTHON_SHARED=On`(default on Unix),
# which need to set a path to .so in LD_LIBRARY_PATH to run `python`.
# For convienience, we build Python twice, SHARED on and off to generate libpython.so and monolithic, statically-liked `python` binary.

function cmake_configure_and_build () {
  # $1 = arg  to BUILD_LIBPYTHON_SHARED
  cmake -G Ninja -DCMAKE_INSTALL_PREFIX:PATH=`pwd`/ci/dist/python \
   -DBUILD_LIBPYTHON_SHARED=$1 \
   -DPYTHON_VERSION="3.10.6" \
   -DUSE_SYSTEM_TCL=OFF \
   -DUSE_SYSTEM_ZLIB=OFF \
   -DUSE_SYSTEM_DB=OFF \
   -DUSE_SYSTEM_GDBM=OFF \
   -DUSE_SYSTEM_LZMA=OFF \
   -DUSE_SYSTEM_READLINE=OFF \
   -DUSE_SYSTEM_SQLITE3=OFF \
   -DENABLE_SSL=OFF \
   -DENABLE_HASHLIB=OFF \
   -DENABLE_MD5=OFF \
   -DENABLE_SHA=OFF \
   -DENABLE_SHA256=OFF \
   -DENABLE_SHA512=OFF \
   -B `pwd`/ci/build_python \
   -S `pwd`/ci/python-cmake-buildsystem && \
   cmake --build `pwd`/ci/build_python --config RelWithDebInfo --clean-first && \
   cmake --install `pwd`/ci/build_python
}

# On, then off so that monolitic `bin/python` is installed to the `ci/dist/python`.
cmake_configure_and_build ON
cmake_configure_and_build OFF
