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

import os
import pytest

import grok_core


def pytest_addoption(parser):
    parser.addoption(
        "--run-s3",
        action="store_true",
        default=False,
        help="Run S3/MinIO integration tests (requires running MinIO on localhost:9000)",
    )


def pytest_configure(config):
    config.addinivalue_line("markers", "s3: S3/MinIO integration tests (need --run-s3)")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--run-s3"):
        return
    skip_s3 = pytest.mark.skip(reason="Need --run-s3 option to run")
    for item in items:
        if "s3" in item.keywords:
            item.add_marker(skip_s3)


@pytest.fixture(scope="session", autouse=True)
def grok_init():
    """Initialize and deinitialize the Grok library once per test session."""
    grok_core.grk_initialize(None, 0, None)
    yield
