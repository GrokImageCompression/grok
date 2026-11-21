# Copyright (C) 2016-2026 Grok Image Compression Inc.
#
# This source code is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License, version 3,
# as published by the Free Software Foundation.
#
# This source code is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

set(PYTHON_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(GROK_BIN_DIR ${CMAKE_BINARY_DIR}/bin)

add_test(
    NAME python_tests
    COMMAND ${Python3_EXECUTABLE} -m pytest
        ${PYTHON_TEST_DIR}
        -v
        --tb=short
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# SWIG module and shared lib live in bin/
set_tests_properties(python_tests PROPERTIES
    ENVIRONMENT "PYTHONPATH=${GROK_BIN_DIR};LD_LIBRARY_PATH=${GROK_BIN_DIR}:$ENV{LD_LIBRARY_PATH}"
    DEPENDS grok_core
)

# GPU plugin tests (opt-in, requires CUDA GPU and plugin)
add_test(
    NAME python_gpu_tests
    COMMAND ${Python3_EXECUTABLE} -m pytest
        ${PYTHON_TEST_DIR}/test_gpu.py
        --run-gpu
        -v
        --tb=short
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

set_tests_properties(python_gpu_tests PROPERTIES
    ENVIRONMENT "PYTHONPATH=${GROK_BIN_DIR};LD_LIBRARY_PATH=${GROK_BIN_DIR}:$ENV{LD_LIBRARY_PATH};CUDA_MODULE_LOADING=EAGER"
    DEPENDS grok_core
    LABELS "gpu"
)
