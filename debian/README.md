----------------
Managing Tags
----------------

Move master tag:

`$ git push origin :refs/tags/v10.0.1 && git tag -fa v10.0.1 && git push origin master --tags`


Move debian/master tag:

`$ git push origin :refs/tags/v10.0.1.debian && git tag -fa v10.0.1.debian && git push origin master --tags`

------------------
Building a Package
------------------

Guide to [setting up schroot](https://wiki.debian.org/Packaging/Pre-Requisites)

0. `cd $SOURCE_DIR`

1. First-time chroot setup:
   `$ apt install git sbuild cmake  devscripts build-essential debhelper help2man libpng-dev libtiff-dev libjpeg-dev zlib1g-dev doxygen lintian libimage-exiftool-perl`

2. `sudo schroot -c debian-sid`

3. `$ git archive --format=tar v10.0.1 | gzip > libgrokj2k_10.0.1.orig.tar.gz && mv libgrokj2k_10.0.1.orig.tar.gz ..`

4. `$ dpkg-buildpackage -us -uc`

or, to just check lintian errors:

   `$ dpkg-buildpackage -S`

5. Check for errors / warnings

   `$ lintian -EviIL +pedantic ../*.changes`


--------------------
Other Architectures
--------------------

[Guide](https://www.antixforum.com/forums/topic/use-sbuild-to-automate-deb-package-building/) to
creating chroots for other architectures.

------------------
GPG Key Management
------------------

https://keyring.debian.org/creating-key.html

https://blog.chapagain.com.np/gpg-remove-keys-from-your-public-keyring/

https://www.linuxbabe.com/security/a-practical-guide-to-gpg-part-1-generate-your-keypair

0. run chroot

1. create gpg key

`$ gpg --full-gen-key`

2. sign `.changes` file

`$ debsign -k 6DFF91ED95915E24EF8CDF2BC23974AB6BA8B412 ../*.changes`

3. dupload changes file

`$ dput -f mentors ../*.changes`

Note: to list all gpg keys: `$ gpg --list-keys`
