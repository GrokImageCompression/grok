#!/bin/bash

# This script executes the script step when running under travis-ci

#if cygwin, check path
case ${MACHTYPE} in
	*cygwin*) GROK_CI_IS_CYGWIN=1;;
	*) ;;
esac

# Hack for appveyor to get GNU find in path before windows one.
export PATH=$(dirname ${BASH}):$PATH

# Set-up some bash options
set -o nounset   ## set -u : exit the script if you try to use an uninitialised variable
set -o errexit   ## set -e : exit the script if any statement returns a non-true return value
set -o pipefail  ## Fail on error in pipe

function opjpath ()
{
	if [ "${GROK_CI_IS_CYGWIN:-}" == "1" ]; then
		cygpath $1 "$2"
	else
		echo "$2"
	fi
}

if [ "${GROK_CI_CC:-}" != "" ]; then
    export CC=${GROK_CI_CC}
    echo "Using ${CC}"
fi

if [ "${GROK_CI_CXX:-}" != "" ]; then
    export CXX=${GROK_CI_CXX}
    echo "Using ${CXX}"
fi

# Set-up some variables
if [ "${GROK_CI_BUILD_CONFIGURATION:-}" == "" ]; then
	export GROK_CI_BUILD_CONFIGURATION=Release #default
fi
GROK_SOURCE_DIR=$(cd $(dirname $0)/../.. && pwd)

if [ "${GROK_DO_SUBMIT:-}" == "" ]; then
	GROK_DO_SUBMIT=0 # Do not flood cdash by default
fi
if [ "${TRAVIS_REPO_SLUG:-}" != "" ]; then
	GROK_OWNER=$(echo "${TRAVIS_REPO_SLUG}" | sed 's/\(^.*\)\/.*/\1/')
	GROK_SITE="${GROK_OWNER}.travis-ci.org"
	if [ "${GROK_OWNER}" == "GrokImageCompression" ]; then
		GROK_DO_SUBMIT=1
	fi
elif [ "${APPVEYOR_REPO_NAME:-}" != "" ]; then
	GROK_OWNER=$(echo "${APPVEYOR_REPO_NAME}" | sed 's/\(^.*\)\/.*/\1/')
	GROK_SITE="${GROK_OWNER}.appveyor.com"
	if [ "${GROK_OWNER}" == "GrokImageCompression" ]; then
		GROK_DO_SUBMIT=1
	fi
else
	GROK_SITE="$(hostname)"
fi

if [ "${TRAVIS_OS_NAME:-}" == "" ]; then
  # Let's guess OS for testing purposes
	echo "Guessing OS"
	if uname -s | grep -i Darwin &> /dev/null; then
		TRAVIS_OS_NAME=osx
	elif uname -s | grep -i Linux &> /dev/null; then
		TRAVIS_OS_NAME=linux
		if [ "${CC:-}" == "" ]; then
			# default to gcc
			export CC=gcc
		fi
	elif uname -s | grep -i CYGWIN &> /dev/null; then
		TRAVIS_OS_NAME=windows
	elif uname -s | grep -i MINGW &> /dev/null; then
		TRAVIS_OS_NAME=windows
	elif [ "${APPVEYOR:-}" == "True" ]; then
		TRAVIS_OS_NAME=windows
	else
		echo "Failed to guess OS"; exit 1
	fi
	echo "${TRAVIS_OS_NAME}"
fi

if [ "${TRAVIS_OS_NAME}" == "osx" ]; then
	GROK_OS_NAME=$(sw_vers -productName | tr -d ' ')$(sw_vers -productVersion | sed 's/\([^0-9]*\.[0-9]*\).*/\1/')
	GROK_CC_VERSION=$(xcodebuild -version | grep -i xcode)
	GROK_CC_VERSION=xcode${GROK_CC_VERSION:6}
elif [ "${TRAVIS_OS_NAME}" == "linux" ]; then
	GROK_OS_NAME=linux
	if which lsb_release > /dev/null; then
		GROK_OS_NAME=$(lsb_release -si)$(lsb_release -sr | sed 's/\([^0-9]*\.[0-9]*\).*/\1/')
	fi
	sudo unlink /usr/bin/gcc && sudo ln -s /usr/bin/gcc-5 /usr/bin/gcc
	gcc --version
	sudo unlink /usr/bin/g++ && sudo ln -s /usr/bin/g++-5 /usr/bin/g++
	g++ --version
	if [ -z "${CC##*gcc*}" ]; then
		GROK_CC_VERSION=$(${CC} --version | head -1 | sed 's/.*\ \([0-9.]*[0-9]\)/\1/')
		if [ -z "${CC##*mingw*}" ]; then
			GROK_CC_VERSION=mingw${GROK_CC_VERSION}
			# disable testing for now
			export GROK_CI_SKIP_TESTS=1
		else
			GROK_CC_VERSION=gcc${GROK_CC_VERSION}
		fi
	elif [ -z "${CC##*clang*}" ]; then
		GROK_CC_VERSION=clang$(${CC} --version | grep version | sed 's/.*version \([^0-9.]*[0-9.]*\).*/\1/')
	else
		echo "Compiler not supported: ${CC}"; exit 1
	fi
elif [ "${TRAVIS_OS_NAME}" == "windows" ]; then
	GROK_OS_NAME=windows
	if which cl > /dev/null; then
		GROK_CL_VERSION=$(cl 2>&1 | grep Version | sed 's/.*Version \([0-9]*\).*/\1/')
		if [ ${GROK_CL_VERSION} -eq 19 ]; then
			GROK_CC_VERSION=vs2015
		elif [ ${GROK_CL_VERSION} -eq 18 ]; then
			GROK_CC_VERSION=vs2013
		elif [ ${GROK_CL_VERSION} -eq 17 ]; then
			GROK_CC_VERSION=vs2012
		elif [ ${GROK_CL_VERSION} -eq 16 ]; then
			GROK_CC_VERSION=vs2010
		elif [ ${GROK_CL_VERSION} -eq 15 ]; then
			GROK_CC_VERSION=vs2008
		elif [ ${GROK_CL_VERSION} -eq 14 ]; then
			GROK_CC_VERSION=vs2005
		else
			GROK_CC_VERSION=vs????
		fi
	fi
else
	echo "OS not supported: ${TRAVIS_OS_NAME}"; exit 1
fi

if [ "${GROK_CI_ARCH:-}" == "" ]; then
	echo "Guessing build architecture"
	MACHINE_ARCH=$(uname -m)
	if [ "${MACHINE_ARCH}" == "x86_64" ]; then
		export GROK_CI_ARCH=x86_64
	fi
	echo "${GROK_CI_ARCH}"
fi

if [ "${TRAVIS_BRANCH:-}" == "" ]; then
	if [ "${APPVEYOR_REPO_BRANCH:-}" != "" ]; then
		TRAVIS_BRANCH=${APPVEYOR_REPO_BRANCH}
	else
		echo "Guessing branch"
		TRAVIS_BRANCH=$(git -C ${GROK_SOURCE_DIR} branch | grep '*' | tr -d '*[[:blank:]]')
	fi
fi

GROK_BUILDNAME=${GROK_OS_NAME}-${GROK_CC_VERSION}-${GROK_CI_ARCH}-${TRAVIS_BRANCH}
GROK_BUILDNAME_TEST=${GROK_OS_NAME}-${GROK_CC_VERSION}-${GROK_CI_ARCH}
if [ "${TRAVIS_PULL_REQUEST:-}" != "false" ] && [ "${TRAVIS_PULL_REQUEST:-}" != "" ]; then
	GROK_BUILDNAME=${GROK_BUILDNAME}-pr${TRAVIS_PULL_REQUEST}
elif [ "${APPVEYOR_PULL_REQUEST_NUMBER:-}" != "" ]; then
	GROK_BUILDNAME=${GROK_BUILDNAME}-pr${APPVEYOR_PULL_REQUEST_NUMBER}
fi
GROK_BUILDNAME=${GROK_BUILDNAME}-${GROK_CI_BUILD_CONFIGURATION}-3rdP
GROK_BUILDNAME_TEST=${GROK_BUILDNAME_TEST}-${GROK_CI_BUILD_CONFIGURATION}-3rdP
if [ "${GROK_CI_ASAN:-}" == "1" ]; then
	GROK_BUILDNAME=${GROK_BUILDNAME}-ASan
	GROK_BUILDNAME_TEST=${GROK_BUILDNAME_TEST}-ASan
fi

if [ "${GROK_NONCOMMERCIAL:-}" == "1" ] && [ "${GROK_CI_SKIP_TESTS:-}" != "1" ] && [ -d kdu ]; then
	echo "
Testing will use Kakadu trial binaries. Here's the copyright notice from kakadu:
Copyright is owned by NewSouth Innovations Pty Limited, commercial arm of the UNSW Australia in Sydney.
You are free to trial these executables and even to re-distribute them,
so long as such use or re-distribution is accompanied with this copyright notice and is not for commercial gain.
Note: Binaries can only be used for non-commercial purposes.
"
fi

if [ -d cmake-install ]; then
	export PATH=${PWD}/cmake-install/bin:${PATH}
fi

set -x
# This will print configuration
# travis-ci doesn't dump cmake version in system info, let's print it 
cmake --version

export TRAVIS_OS_NAME=${TRAVIS_OS_NAME}
export GROK_SITE=${GROK_SITE}
export GROK_BUILDNAME=${GROK_BUILDNAME}
export GROK_SOURCE_DIR=$(opjpath -m ${GROK_SOURCE_DIR})
export GROK_BINARY_DIR=$(opjpath -m ${PWD}/build)
export GROK_BUILD_CONFIGURATION=${GROK_CI_BUILD_CONFIGURATION}
export GROK_DO_SUBMIT=${GROK_DO_SUBMIT}

if [ "${GROK_SKIP_REBUILD:-}" != "1" ]; then
    ctest -S ${GROK_SOURCE_DIR}/tools/ctest_scripts/travis-ci.cmake -V || true
fi
# ctest will exit with various error codes depending on version.
# ignore ctest exit code & parse this ourselves
set +x



if [ "${GROK_CI_CHECK_STYLE:-}" == "1" ]; then
    export OPJSTYLE=${PWD}/scripts/opjstyle
    export PATH=${HOME}/.local/bin:${PATH}
    scripts/verify-indentation.sh
fi


# Deployment if needed
#---------------------
if [ "${TRAVIS_TAG:-}" != "" ]; then
		GROK_TAG_NAME=${TRAVIS_TAG}
	elif [ "${APPVEYOR_REPO_TAG:-}" == "true" ]; then
		GROK_TAG_NAME=${APPVEYOR_REPO_TAG_NAME}
	else
		GROK_TAG_NAME=""
	fi
if [ "${GROK_CI_INCLUDE_IF_DEPLOY:-}" == "1" ] && [ "${GROK_TAG_NAME:-}" != "" ]; then
#if [ "${GROK_CI_INCLUDE_IF_DEPLOY:-}" == "1" ]; then
	GROK_CI_DEPLOY=1		# unused for now
	GROK_CUR_DIR=${PWD}
	if [ "${TRAVIS_OS_NAME:-}" == "linux" ]; then
		GROK_PACK_GENERATOR="TGZ" # ZIP generator currently segfaults on linux
	else
		GROK_PACK_GENERATOR="ZIP"
	fi
	GROK_PACK_NAME="grok-${GROK_TAG_NAME}-${TRAVIS_OS_NAME}-${GROK_CI_ARCH}"
	cd ${GROK_BINARY_DIR}
	cmake -D CPACK_GENERATOR:STRING=${GROK_PACK_GENERATOR} -D CPACK_PACKAGE_FILE_NAME:STRING=${GROK_PACK_NAME} ${GROK_SOURCE_DIR}
	cd ${GROK_CUR_DIR}
	cmake --build ${GROK_BINARY_DIR} --target package
	echo "ready to deploy $(ls ${GROK_BINARY_DIR}/${GROK_PACK_NAME}*) to GitHub releases"
	if [ "${APPVEYOR_REPO_TAG:-}" == "true" ]; then
		appveyor PushArtifact "${GROK_BINARY_DIR}/${GROK_PACK_NAME}.zip"
	fi
else
	GROK_CI_DEPLOY=0
fi

# let's parse configure/build/tests for failure

echo "
Parsing logs for failures
"
GROK_CI_RESULT=0

# 1st configure step
GROK_CONFIGURE_XML=$(find build -path 'build/Testing/*' -name 'Configure.xml')
if [ ! -f "${GROK_CONFIGURE_XML}" ]; then
	echo "No configure log found"
	GROK_CI_RESULT=1
else
	if ! grep '<ConfigureStatus>0</ConfigureStatus>' ${GROK_CONFIGURE_XML} &> /dev/null; then
		echo "Errors were found in configure log"
		GROK_CI_RESULT=1
	fi
fi

# 2nd build step
# We must have one Build.xml file
GROK_BUILD_XML=$(find build -path 'build/Testing/*' -name 'Build.xml')
if [ ! -f "${GROK_BUILD_XML}" ]; then
	echo "No build log found"
	GROK_CI_RESULT=1
else
	if grep '<Error>' ${GROK_BUILD_XML} &> /dev/null; then
		echo "Errors were found in build log"
		GROK_CI_RESULT=1
	fi
fi

if [ ${GROK_CI_RESULT} -ne 0 ]; then
	# Don't trash output with failing tests when there are configure/build errors
	exit ${GROK_CI_RESULT}
fi

if [ "${GROK_CI_SKIP_TESTS:-}" != "1" ]; then
	GROK_TEST_XML=$(find build -path 'build/Testing/*' -name 'Test.xml')
	if [ ! -f "${GROK_TEST_XML}" ]; then
		echo "No test log found"
		GROK_CI_RESULT=1
	else
		echo "Parsing tests for new/unknown failures"
		# 3rd test step
		GROK_FAILEDTEST_LOG=$(find build -path 'build/Testing/Temporary/*' -name 'LastTestsFailed_*.log')
		if [ -f "${GROK_FAILEDTEST_LOG}" ]; then
			awk -F: '{ print $2 }' ${GROK_FAILEDTEST_LOG} > failures.txt
			while read FAILEDTEST; do
				# Start with common errors
				if grep -x "${FAILEDTEST}" $(opjpath -u ${GROK_SOURCE_DIR})/tools/travis-ci/knownfailures-all.txt > /dev/null; then
					continue
				fi
				if [ -f $(opjpath -u ${GROK_SOURCE_DIR})/tools/travis-ci/knownfailures-${GROK_BUILDNAME_TEST}.txt ]; then
					if grep -x "${FAILEDTEST}" $(opjpath -u ${GROK_SOURCE_DIR})/tools/travis-ci/knownfailures-${GROK_BUILDNAME_TEST}.txt > /dev/null; then
						continue
					fi
				fi
				echo "${FAILEDTEST}"
				GROK_CI_RESULT=1
			done < failures.txt
		fi
	fi
	
	if [ ${GROK_CI_RESULT} -eq 0 ]; then
		echo "No new/unknown test failure found
		"
	else
		echo "
New/unknown test failure found!!!
	"
	fi
	
	# 4th memcheck step
	GROK_MEMCHECK_XML=$(find build -path 'build/Testing/*' -name 'DynamicAnalysis.xml')
	if [ -f "${GROK_MEMCHECK_XML}" ]; then
		if grep '<Defect Type' ${GROK_MEMCHECK_XML} 2> /dev/null; then
			echo "Errors were found in dynamic analysis log"
			GROK_CI_RESULT=1
		fi
	fi
fi

if [ "${GROK_CI_PERF_TESTS:-}" == "1" ]; then
    cd tests/performance
    echo "Running performance tests on current version (dry-run)"
    PATH=../../build/bin:$PATH python ./perf_test.py
    echo "Running performance tests on current version"
    PATH=../../build/bin:$PATH python ./perf_test.py -o /tmp/new.csv
    if [ "${GROK_NONCOMMERCIAL:-}" == "1" ] && [ -d ../../kdu ]; then
        echo "Running performances tests with Kakadu"
        LD_LIBRARY_PATH=../../kdu PATH=../../kdu::$PATH python ./perf_test.py -kakadu -o /tmp/kakadu.csv
        echo "Comparing current version with Kakadu"
        python compare_perfs.py /tmp/kakadu.csv /tmp/new.csv || true
    fi
    cd ../..

    REF_VERSION=master
    if [ "${TRAVIS_PULL_REQUEST:-false}" == "false" ]; then
        REF_VERSION=v2.1.2
    fi
    if [ ! -d ref_opj ]; then
        git clone https://github.com/GrokImageCompression/grok ref_opj
    fi
    echo "Building reference version (${REF_VERSION})"
    cd ref_opj
    git checkout ${REF_VERSION}
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=${GROK_BUILD_CONFIGURATION}
    make -j3
    cd ../..
    cd tests/performance
    echo "Running performance tests on ${REF_VERSION} version (dry-run)"
    PATH=../../ref_opj/build/bin:$PATH python ./perf_test.py
    echo "Running performance tests on ${REF_VERSION} version"
    PATH=../../ref_opj/build/bin:$PATH python ./perf_test.py -o /tmp/ref.csv
    echo "Comparing current version with ${REF_VERSION} version"
    # we should normally set GROK_CI_RESULT=1 in case of failure, but
    # this is too unreliable
    python compare_perfs.py /tmp/ref.csv /tmp/new.csv || true
    cd ../..
fi

if [ "${GROK_CI_PROFILE:-}" == "1" ]; then
    rm -rf build_gprof
    mkdir build_gprof
    cd build_gprof
    # We need static linking for gprof
    cmake "-DCMAKE_C_FLAGS=-pg -O3" -DCMAKE_EXE_LINKER_FLAGS=-pg -DCMAKE_SHARED_LINKER_FLAGS=-pg -DBUILD_SHARED_LIBS=OFF ..
    make -j3
    cd ..
    build_gprof/bin/opj_decompress -i data/input/nonregression/kodak_2layers_lrcp.j2c -o out.tif > /dev/null
    echo "Most CPU consuming functions:"
    gprof build_gprof/bin/opj_decompress gmon.out | head || true

    rm -f massif.out.*
    valgrind --tool=massif build/bin/opj_decompress -i data/input/nonregression/kodak_2layers_lrcp.j2c -o out.tif >/dev/null 2>/dev/null
    echo ""
    echo "Memory consumption profile:"
    python tests/profiling/filter_massif_output.py massif.out.*
fi

exit ${GROK_CI_RESULT}
