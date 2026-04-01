----------------
Managing Tags
----------------

Move master tag:

`$ git push origin :refs/tags/v20.2.5 && git tag -fa v20.2.5 && git push origin --tags`


Move debian/master tag:

`$ git push origin :refs/tags/v20.2.5.debian && git tag -fa v20.2.5.debian && git push origin --tags`

------------------
Building a Package
------------------

Guide to [setting up schroot](https://wiki.debian.org/Packaging/Pre-Requisites)

0. `cd $SOURCE_DIR`

1. First-time chroot setup:
   `$ apt install git sbuild cmake  devscripts build-essential debhelper help2man libpng-dev liblcms2-dev libtiff-dev libjpeg-dev zlib1g-dev doxygen lintian libimage-exiftool-perl`

2. `sudo schroot -c debian-sid`

3. `$ git archive --format=tar v20.2.5 | gzip > libgrokj2k_20.2.5.orig.tar.gz && mv libgrokj2k_20.2.5.orig.tar.gz ..`

4. `$ dpkg-buildpackage -us -uc`

or, to just check lintian errors:

   `$ dpkg-buildpackage -S`

5. Check for errors / warnings

   `$ lintian -EviIL +pedantic ../*.changes`


-------------------------------------------
Setting up a Debian SID chroot on Fedora
-------------------------------------------

Fedora does not ship `sbuild` or `schroot`, so use `debootstrap` and
`systemd-nspawn` to create a minimal Debian SID environment for building
packages.

### Prerequisites

```
$ sudo dnf install debootstrap systemd-container debian-keyring
```

### Create the chroot

```
$ sudo debootstrap --arch=amd64 sid /srv/chroot/debian-sid http://deb.debian.org/debian
```

### Enter the chroot

```
$ sudo systemd-nspawn -D /srv/chroot/debian-sid
```

### First-time setup inside the chroot

```
# apt update && apt upgrade -y
# apt install -y git cmake build-essential debhelper devscripts help2man \
    libpng-dev liblcms2-dev libtiff-dev libjpeg-dev zlib1g-dev doxygen \
    lintian libimage-exiftool-perl
```

### Bind-mount source directory and build

From the Fedora host:

```
$ sudo systemd-nspawn -D /srv/chroot/debian-sid --bind=/home/aaron/src/grok:/src/grok
# cd /src/grok
# git archive --format=tar v20.2.5 | gzip > ../libgrokj2k_20.2.5.orig.tar.gz
# dpkg-buildpackage -us -uc
# lintian -EviIL +pedantic ../*.changes
```

### Re-entering the chroot later

```
$ sudo systemd-nspawn -D /srv/chroot/debian-sid --bind=/home/aaron/src/grok:/src/grok
```


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

Choose default RSA key, and choose length of 4096 bits

2. sign `.changes` file

`$ debsign -k AD89FCBE49DCDB2F4DE41D4E9E763CFFF2AC6581 ../*.changes`

3. dupload changes file

`$ dput -f mentors ../*.changes`

Note: to list all gpg keys: `$ gpg --list-keys`
