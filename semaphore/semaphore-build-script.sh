# This is the build script used for
# https://semaphoreci.com/libratbag/libratbag/
#
# semaphore uses this script directly, use the following two commands as
# semaphore's build script:
#  curl  https://raw.githubusercontent.com/libratbag/libratbag/master/semaphore/semaphore-build-script.sh > build-script.sh
#  sh ./build-script.sh

sudo apt-get update
sudo apt-get install -y valgrind check libevdev-dev libudev-dev doxygen graphviz
autoreconf -ivf
./configure --prefix=$PWD/_build --with-udev-base-dir=$PWD/_build
DISTCHECK_CONFIGURE_FLAGS="--with-udev-base-dir=$PWD/_build" make distcheck
