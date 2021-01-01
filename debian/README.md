----------------
Useful Commands
----------------

Move master tag:

`$ git push origin :refs/tags/v7.6.3 && git tag -fa v7.6.3 && git push origin master --tags`


Move debian/master tag:

`$ git push origin :refs/tags/v7.6.3.debian && git tag -fa v7.6.3.debian && git push origin debian/master --tags`

------------------
Building a Package
------------------

Guide to [setting up schroot](https://wiki.debian.org/Packaging/Pre-Requisites)
`$ apt install git sbuild cmake  debhelper help2man liblcms2-dev libpng-dev libzstd-dev libtiff-dev libjpeg-dev zlib1g-dev doxygen lintian`


(In steps below, we are packaging version 7.6.2, using GPG key id
B1D6B0917E6191EB3D0FF95F347F22FFCA601A1C)

0. `cd $SOURCE_DIR`

1. `sudo schroot -c debian-sid`

2. `$ git archive --format=tar v7.6.3 | gzip > libgrokj2k_7.6.3.orig.tar.gz && mv libgrokj2k_7.6.3.orig.tar.gz ..`

3. `$ dpkg-buildpackage -us -uc`

or, to just check lintian errors:

   `$ dpkg-buildpackage -S`

3. Check for errors / warnings

   `$ lintian -EviIL +pedantic ../*.changes`
   
   
------------------
GPG Key Management
------------------

1. create gpg key

`$ gpg --gen-key`

2. sign .changes file

`$ debsign -k B1D6B0917E6191EB3D0FF95F347F22FFCA601A1C ../*.changes`

3. dupload changes file

`$ dput -f mentors ../*.changes`

https://keyring.debian.org/creating-key.html

`$ gpg --list-keys`
