This directory contain fuzzer main functions and scripts for the
Google [oss-fuzz](https://github.com/google/oss-fuzz/) project

The main build scripts can be found [here](https://github.com/google/oss-fuzz/tree/master/projects/grok)
and call scripts in this directory.

The list of issues can be found [here](https://bugs.chromium.org/p/oss-fuzz/issues/list?q=grok)

Fuzzer build logs can be found [here](https://oss-fuzz-build-logs.storage.googleapis.com/index.html#grok)


### Simulate fuzzer

#### Preliminary steps

```   
    $ cd /tmp
    $ git clone https://github.com/GrokImageCompression/grok
    $ cd grok
    $ git clone --depth 1 https://github.com/GrokImageCompression/grok-test-data data
    $ mkdir build
    $ cd build
    $ cmake ..
    $ make
    $ cd ..
```

#### Build fuzzer and seed corpus

 ```  
    $ cd tests/fuzzers
    $ make
```

Fuzzers created in `/tmp/*_fuzzer`, with `/tmp/*_fuzzer_seed_corpus.zip` corpus files

#### Run a particular fuzzer

 ```
    $ /tmp/grk_decompress_fuzzer a_file_name
```

### Run oss-fuzz locally

```
    $ git clone https://github.com/google/oss-fuzz.git
    $ cd oss-fuzz
    $ python infra/helper.py build_image grok
```

#### Build fuzzers with the address sanitizer (could use undefined, etc...)

```  
    $ python infra/helper.py build_fuzzers --sanitizer address grok
```

Test a particular fuzzer (replace grk_decompress_fuzzer by other fuzzers
like the ones generated in /tmp by "make dummyfuzzers")

```  
    $ python infra/helper.py run_fuzzer grok grk_decompress_fuzzer
```

Test a particular fuzzer on a test file:

```  
$ python infra/helper.py build_fuzzers --sanitizer address grok
$ python infra/helper.py reproduce grok grk_decompress_fuzzer $FILE_NAME
```


Handling issues reported in https://bugs.chromium.org/p/oss-fuzz/issues/list?q=grok


  1. Leave a comment in (chromium database) bug entry to indicate that you work on it
  1. Work
  1. Commit a bug fix with log including "Credit to OSS-Fuzz" and a link to the bugs.chromium.org ticket
  1. Add in the bugs.chromium.org ticket a link to the github commit implementing the fix.
  1. Check chromium closed the bug (after one or two days typically)
