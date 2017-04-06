# This is the build script used for
# https://semaphoreci.com/libratbag/libratbag/
#
# semaphore uses this script directly, use the following two commands as
# semaphore's build script:
#  curl  https://raw.githubusercontent.com/libratbag/libratbag/master/semaphore/semaphore-build-script.sh > build-script.sh
#  sh ./build-script.sh

sudo apt-get update
sudo apt-get install -y valgrind check libevdev-dev libudev-dev doxygen graphviz

# no meson on 14.04
sudo apt-get install -y python3-pip
sudo pip3 install meson

# ninja on 14.04 is too old
git clone git://github.com/ninja-build/ninja.git
cd ninja
git checkout release
./configure.py --bootstrap
sudo cp ninja /usr/bin/ninja
cd ..

meson builddir
ninja -C builddir test
