------------------
Building a Package
------------------

(In steps below, we are packaging version 7.0.0, using GPG key id
52635FF060C9659C720E3A2F59A8A51BC41CB925)

0. cd $SOURCE_DIR

1. $ git archive --format=tar master | gzip > libgrokj2k_7.0.0.orig.tar.gz

2. $ cp libgrokj2k_7.0.0.orig.tar.gz ..

3. $ dpkg-buildpackage -us -uc

------------------
GPG Key Management
------------------

1. create gpg key
gpg --gen-key

2. upload key to debian
gpg --keyserver pool.sks-keyservers.net --send-key '52635FF060C9659C720E3A2F59A8A51BC41CB925'

3. sign .changes file
debsign -k 52635FF060C9659C720E3A2F59A8A51BC41CB925 *.changes

4. dupload changes file
dupload *.changes

https://keyring.debian.org/creating-key.html
gpg --list-keys
