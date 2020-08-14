autopkg testsuite


Git - Move Tag

1. Delete old tag

`git push origin :refs/tags/v7.6.0`

2. Replace the tag to reference the most recent commit

`git tag -fa v7.6.0`

3. Push the new tag to remote origin

`git push origin master --tags`

`$ git push origin :refs/tags/v7.6.0 && git tag -fa v7.6.0 && git push origin master --tags`

------------------
Building a Package
------------------

(In steps below, we are packaging version 7.6.0, using GPG key id
B1D6B0917E6191EB3D0FF95F347F22FFCA601A1C)

0. `cd $SOURCE_DIR`

1. `sudo schroot -c debian-sid`

2. `$ git archive --format=tar v7.6.0 | gzip > libgrokj2k_7.6.0.orig.tar.gz && mv libgrokj2k_7.6.0.orig.tar.gz ..`

3. `$ dpkg-buildpackage -us -uc`

or, to just check lintian errors:

   `$ dpkg-buildpackage -S`

3. Check for errors / warnings

   `$ lintian -EviIL +pedantic ../*.changes`
   
   
4. Symbols file

```
$ dpkg-gensymbols -plibgrokj2k -Olibgrokj2k1.symbols
$ sed 's/ \(_.*\) \(.*\)/ (c++)"\1" \2/' libgrokj2k1.symbols | c++filt
```

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
