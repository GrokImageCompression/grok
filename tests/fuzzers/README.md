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

(make sure that `docker.io` is installed)

#### Build grok image

```
    $ git clone https://github.com/google/oss-fuzz.git
    $ cd oss-fuzz
    $ sudo python3 infra/helper.py build_image grok
```

#### Build fuzzers with the address sanitizer (could use undefined, etc...)

```  
    $ sudo python3 infra/helper.py build_fuzzers --sanitizer address grok
```

Test a particular fuzzer (replace grk_decompress_fuzzer by other fuzzers
like the ones generated in /tmp by "make dummyfuzzers")

```  
    $ sudo python3 infra/helper.py run_fuzzer grok grk_decompress_fuzzer
```

Test a particular fuzzer on a test file:

```  
$ sudo python3 infra/helper.py reproduce grok grk_decompress_fuzzer $FILE_NAME
```

#### Fetch reproducer backup

Reproducers are periodically backed up on Google Cloud. 
A zip file of these reproducers can be downloaded as follows:

First of all, ensure that Google Cloud SDK is installed:

```
$ sudo snap install google-cloud-sdk --classic
$ gcloud init
```

Now, we can list the contents of our fuzzer bucket:

`$ gsutil ls -R gs://grok-backup.clusterfuzz-external.appspot.com/corpus/libFuzzer/**/public.zip`

and then copy the zip to a local drive:

`$ gsutil cp gs://grok-backup.clusterfuzz-external.appspot.com/corpus/libFuzzer/grok_grk_decompress_fuzzer/public.zip .`

#### Batch testing of reproducers

The following shell script run in the directory holding the reproducers will compare Grok output with `kdu_expand` :

```
( for i in *; do echo -e "${i} \n"; kdu_expand -i $i -o ../fuzz_out/$i.tiff; echo -e "\n" ;grk_decompress -i $i -o ../fuzz_out/$i.tiff;  echo -e "\n\n";  done; ) |  tee ~/output.txt
```




