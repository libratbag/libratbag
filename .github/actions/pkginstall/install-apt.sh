#!/bin/bash -x

# If called without arguments, just skip the rest
if [[ -z "$@" ]]; then
	exit
fi

# Don't care about these bits
echo 'path-exclude=/usr/share/doc/*' > /etc/dpkg/dpkg.cfg.d/99-exclude-cruft
echo 'path-exclude=/usr/share/locale/*' >> /etc/dpkg/dpkg.cfg.d/99-exclude-cruft
echo 'path-exclude=/usr/share/man/*' >> /etc/dpkg/dpkg.cfg.d/99-exclude-cruft

apt-get update
apt-get install -yq --no-install-suggests --no-install-recommends $@
