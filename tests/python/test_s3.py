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

"""S3/MinIO integration tests for Grok decompression.

These tests require a running MinIO instance on localhost:9000.
They are disabled by default — run with:

    pytest --run-s3

Or just the S3 tests:

    pytest --run-s3 -m s3
"""

import os
import socket
import urllib.request
import urllib.error
import hashlib

import pytest

try:
    import grok_codec
except ImportError:
    grok_codec = None


MINIO_ENDPOINT = os.environ.get("MINIO_ENDPOINT", "localhost:9000")
MINIO_ACCESS_KEY = os.environ.get("AWS_ACCESS_KEY_ID", "minioadmin")
MINIO_SECRET_KEY = os.environ.get("AWS_SECRET_ACCESS_KEY", "minioadmin")
MINIO_SECURE = os.environ.get("MINIO_SECURE", "true").lower() in ("true", "yes", "1")
MINIO_SCHEME = "https" if MINIO_SECURE else "http"
BUCKET = "grok-test"


def minio_is_reachable():
    """Check if MinIO is listening on the configured endpoint."""
    host, _, port = MINIO_ENDPOINT.partition(":")
    port = int(port) if port else 9000
    try:
        with socket.create_connection((host, port), timeout=2):
            return True
    except (OSError, ConnectionRefusedError):
        return False


def make_pgm(path, width=32, height=32):
    """Create a small PGM (grayscale) test image."""
    pixels = bytes((x + y) % 256 for y in range(height) for x in range(width))
    with open(path, "wb") as f:
        f.write(f"P5\n{width} {height}\n255\n".encode())
        f.write(pixels)
    return path


def make_jp2(tmp_path, filename="s3_test.jp2", extra_args=None, width=128, height=128):
    """Create a test JP2 file via grk_codec_compress."""
    pgm_path = str(tmp_path / "input.pgm")
    make_pgm(pgm_path, width=width, height=height)
    jp2_path = str(tmp_path / filename)
    args = ["grk_compress", "-i", pgm_path, "-o", jp2_path]
    if extra_args:
        args.extend(extra_args)
    rc = grok_codec.grk_codec_compress(args, None, None)
    assert rc == 0, f"grk_codec_compress failed with rc={rc}"
    assert os.path.getsize(jp2_path) > 0
    return jp2_path


def minio_put_object(bucket, key, filepath):
    """Upload a file to MinIO using the S3 REST API (PUT with unsigned request).

    MinIO allows anonymous uploads if the bucket policy permits it.
    For test setup we use mc CLI or fall back to creating a public bucket.
    """
    import subprocess

    # Use mc (MinIO Client) if available — simplest approach
    mc = None
    for name in ("mc", "mcli"):
        try:
            subprocess.run([name, "--version"], capture_output=True, check=True)
            mc = name
            break
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue

    if mc:
        # --insecure needed for self-signed TLS certs
        insecure = ["--insecure"] if MINIO_SECURE else []
        # Configure alias and upload
        subprocess.run(
            [mc, *insecure, "alias", "set", "groktest",
             f"{MINIO_SCHEME}://{MINIO_ENDPOINT}", MINIO_ACCESS_KEY, MINIO_SECRET_KEY],
            capture_output=True, check=True,
        )
        # Create bucket (ignore error if exists)
        subprocess.run(
            [mc, *insecure, "mb", "--ignore-existing", f"groktest/{bucket}"],
            capture_output=True, check=True,
        )
        # Set bucket to public (so unsigned reads work for http:// URL tests)
        subprocess.run(
            [mc, *insecure, "anonymous", "set", "download", f"groktest/{bucket}"],
            capture_output=True,
        )
        # Upload
        subprocess.run(
            [mc, *insecure, "cp", filepath, f"groktest/{bucket}/{key}"],
            capture_output=True, check=True,
        )
        return

    # Fallback: use boto3 if available
    try:
        import boto3
        from botocore.client import Config as BotoConfig

        client = boto3.client(
            "s3",
            endpoint_url=f"{MINIO_SCHEME}://{MINIO_ENDPOINT}",
            aws_access_key_id=MINIO_ACCESS_KEY,
            aws_secret_access_key=MINIO_SECRET_KEY,
            config=BotoConfig(signature_version="s3v4"),
            region_name="us-east-1",
            verify=not MINIO_SECURE,  # skip cert verification for self-signed
        )
        try:
            client.create_bucket(Bucket=bucket)
        except client.exceptions.BucketAlreadyOwnedByYou:
            pass
        client.upload_file(filepath, bucket, key)
        return
    except ImportError:
        pass

    pytest.skip("Neither mc/mcli CLI nor boto3 available for MinIO upload")


@pytest.fixture(scope="module")
def jp2_on_minio(tmp_path_factory):
    """Upload a test JP2 to MinIO and return (bucket, key, local_path)."""
    tmp_path = tmp_path_factory.mktemp("s3")
    jp2_path = make_jp2(tmp_path)
    key = "test.jp2"
    minio_put_object(BUCKET, key, jp2_path)
    return BUCKET, key, jp2_path


pytestmark = [
    pytest.mark.s3,
    pytest.mark.skipif(grok_codec is None, reason="grok_codec module not available"),
]


@pytest.fixture(autouse=True)
def s3_env(monkeypatch):
    """Set S3Fetcher env vars for MinIO access."""
    monkeypatch.setenv("AWS_S3_ENDPOINT", MINIO_ENDPOINT)
    monkeypatch.setenv("AWS_ACCESS_KEY_ID", MINIO_ACCESS_KEY)
    monkeypatch.setenv("AWS_SECRET_ACCESS_KEY", MINIO_SECRET_KEY)
    monkeypatch.setenv("AWS_VIRTUAL_HOSTING", "FALSE")
    if MINIO_SECURE:
        monkeypatch.setenv("AWS_HTTPS", "YES")
        monkeypatch.setenv("GRK_CURL_ALLOW_INSECURE", "YES")
    else:
        monkeypatch.setenv("AWS_HTTPS", "NO")


@pytest.mark.s3
class TestS3Decompress:
    def test_decompress_vsis3(self, jp2_on_minio, tmp_path):
        """Decompress from MinIO via /vsis3/ path."""
        bucket, key, local_jp2 = jp2_on_minio
        out_path = str(tmp_path / "output_vsis3.pgm")

        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", f"/vsis3/{bucket}/{key}", "-o", out_path]
        )
        assert rc == 0, f"grk_codec_decompress (vsis3) failed with rc={rc}"
        assert os.path.exists(out_path)
        assert os.path.getsize(out_path) > 0

    def test_decompress_http_url(self, jp2_on_minio, tmp_path):
        """Decompress from MinIO via direct HTTPS URL."""
        bucket, key, local_jp2 = jp2_on_minio
        out_path = str(tmp_path / "output_http.pgm")

        url = f"{MINIO_SCHEME}://{MINIO_ENDPOINT}/{bucket}/{key}"
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", url, "-o", out_path]
        )
        assert rc == 0, f"grk_codec_decompress (http) failed with rc={rc}"
        assert os.path.exists(out_path)
        assert os.path.getsize(out_path) > 0

    def test_decompress_vsis3_matches_local(self, jp2_on_minio, tmp_path):
        """Verify S3 decompression produces the same output as local decompression."""
        bucket, key, local_jp2 = jp2_on_minio

        # Decompress locally
        local_out = str(tmp_path / "local.pgm")
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", local_jp2, "-o", local_out]
        )
        assert rc == 0

        # Decompress from S3
        s3_out = str(tmp_path / "s3.pgm")
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", f"/vsis3/{bucket}/{key}", "-o", s3_out]
        )
        assert rc == 0

        # Compare file contents
        with open(local_out, "rb") as f:
            local_hash = hashlib.sha256(f.read()).hexdigest()
        with open(s3_out, "rb") as f:
            s3_hash = hashlib.sha256(f.read()).hexdigest()
        assert local_hash == s3_hash, "S3 decompress output differs from local"

    def test_decompress_vsis3_to_png(self, jp2_on_minio, tmp_path):
        """Decompress from MinIO via /vsis3/ to PNG format."""
        bucket, key, local_jp2 = jp2_on_minio
        out_path = str(tmp_path / "output_vsis3.png")

        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", f"/vsis3/{bucket}/{key}", "-o", out_path]
        )
        assert rc == 0
        assert os.path.getsize(out_path) > 0

    def test_decompress_nonexistent_s3_key(self, jp2_on_minio, tmp_path):
        """Decompress a nonexistent S3 key should return non-zero."""
        bucket, _, _ = jp2_on_minio
        out_path = str(tmp_path / "output.pgm")

        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", f"/vsis3/{bucket}/nonexistent.jp2", "-o", out_path]
        )
        assert rc != 0


# Parameterized tile/TLM configurations:
#   (description, extra_compress_args, multi_tile)
TILE_TLM_CONFIGS = [
    ("single_tile_with_tlm", ["-X"], False),
    ("single_tile_no_tlm", [], False),
    ("multi_tile_with_tlm", ["-t", "64,64", "-X"], True),
    ("multi_tile_no_tlm", ["-t", "64,64"], True),
]


@pytest.fixture(scope="module", params=TILE_TLM_CONFIGS, ids=[c[0] for c in TILE_TLM_CONFIGS])
def tile_tlm_on_minio(request, tmp_path_factory):
    """Compress with tile/TLM config, upload to MinIO, return metadata."""
    desc, extra_args, multi_tile = request.param
    tmp_path = tmp_path_factory.mktemp(desc)
    filename = f"{desc}.jp2"
    jp2_path = make_jp2(tmp_path, filename=filename, extra_args=extra_args)
    key = filename
    minio_put_object(BUCKET, key, jp2_path)
    return desc, key, jp2_path, multi_tile


@pytest.mark.s3
class TestS3TileTLM:
    def test_decompress_vsis3(self, tile_tlm_on_minio, tmp_path):
        """Decompress tile/TLM variant from S3 via /vsis3/."""
        desc, key, local_jp2, multi_tile = tile_tlm_on_minio
        out_path = str(tmp_path / f"{desc}_output.pgm")

        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", f"/vsis3/{BUCKET}/{key}", "-o", out_path]
        )
        assert rc == 0, f"S3 decompress failed for {desc}"
        assert os.path.getsize(out_path) > 0

    def test_s3_matches_local(self, tile_tlm_on_minio, tmp_path):
        """Verify S3 decompress matches local for each tile/TLM variant."""
        desc, key, local_jp2, multi_tile = tile_tlm_on_minio

        local_out = str(tmp_path / "local.pgm")
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", local_jp2, "-o", local_out]
        )
        assert rc == 0

        s3_out = str(tmp_path / "s3.pgm")
        rc = grok_codec.grk_codec_decompress(
            ["grk_decompress", "-i", f"/vsis3/{BUCKET}/{key}", "-o", s3_out]
        )
        assert rc == 0

        with open(local_out, "rb") as f:
            local_hash = hashlib.sha256(f.read()).hexdigest()
        with open(s3_out, "rb") as f:
            s3_hash = hashlib.sha256(f.read()).hexdigest()
        assert local_hash == s3_hash, f"S3 output differs from local for {desc}"
