----------------
Useful Commands
----------------

Move master tag:

`$ git push origin :refs/tags/v7.6.5 && git tag -fa v7.6.5 && git push origin master --tags`


Move debian/master tag:

`$ git push origin :refs/tags/v7.6.5.debian && git tag -fa v7.6.5.debian && git push origin debian/master --tags`

------------------
Building a Package
------------------

Guide to [setting up schroot](https://wiki.debian.org/Packaging/Pre-Requisites)
`$ apt install git sbuild cmake  devscripts build-essential debhelper help2man liblcms2-dev libpng-dev libzstd-dev libtiff-dev libjpeg-dev zlib1g-dev doxygen lintian libimage-exiftool-perl`

0. `cd $SOURCE_DIR`

1. `sudo schroot -c debian-sid`

2. `$ git archive --format=tar v7.6.5 | gzip > libgrokj2k_7.6.5.orig.tar.gz && mv libgrokj2k_7.6.5.orig.tar.gz ..`

3. `$ dpkg-buildpackage -us -uc`

or, to just check lintian errors:

   `$ dpkg-buildpackage -S`

3. Check for errors / warnings

   `$ lintian -EviIL +pedantic ../*.changes`
   
   
------------------
GPG Key Management
------------------

Guides to configuring gpg : note: need to generate 4096 bit keys

https://keyring.debian.org/creating-key.html
https://blog.chapagain.com.np/gpg-remove-keys-from-your-public-keyring/
https://www.linuxbabe.com/security/a-practical-guide-to-gpg-part-1-generate-your-keypair


1. create gpg key

`$ gpg --full-gen-key`

2. sign .changes file

`$ debsign -k CA02AD91A2CD5E4875BFE20292D3B561135ECB52 ../*.changes`

3. dupload changes file

`$ dput -f mentors ../*.changes`

https://keyring.debian.org/creating-key.html

`$ gpg --list-keys`
