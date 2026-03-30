#!/usr/bin/env python3
"""
Download LastTest.log artifacts from the most recent GitHub Actions CI run
and update the platform-specific md5refs files.

Requirements: gh CLI authenticated (gh auth login).

Usage:
    python tests/nonregression/collect_ci_md5refs.py
    python tests/nonregression/collect_ci_md5refs.py --run-id 1234567890
    python tests/nonregression/collect_ci_md5refs.py --dry-run
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = "GrokImageCompression/grok"
SCRIPT_DIR = Path(__file__).parent

# Maps artifact name pattern -> platform key for update_md5refs.py
# Artifact names: test-results-<os>-shared_<ON|OFF>
# Platform keys follow CI matrix naming: <os>-<dynamic|static>
# ubuntu-dynamic is canonical (md5refs.txt) — no platform override needed.
ARTIFACT_PLATFORM_MAP = {
    ("macos-latest",  "ON"):  "macos-dynamic",
    ("macos-latest",  "OFF"): "macos-static",
    ("windows-latest","ON"):  "windows-dynamic",
    ("windows-latest","OFF"): "windows-static",
    ("ubuntu-latest", "ON"):  None,   # canonical md5refs.txt — skip
    ("ubuntu-latest", "OFF"): "ubuntu-static",
}


def run(cmd, **kwargs):
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **kwargs)


def get_latest_run_id(repo):
    result = run(["gh", "run", "list", "--repo", repo, "--workflow", "build.yml",
                  "--limit", "1", "--json", "databaseId"])
    runs = json.loads(result.stdout)
    if not runs:
        sys.exit("No completed runs found for build.yml")
    return str(runs[0]["databaseId"])


def list_artifacts(repo, run_id):
    result = run(["gh", "run", "view", run_id, "--repo", repo, "--json", "jobs"])
    # list artifacts via api
    result = run(["gh", "api", f"repos/{repo}/actions/runs/{run_id}/artifacts",
                  "--paginate"])
    data = json.loads(result.stdout)
    return data.get("artifacts", [])


def download_artifact(repo, artifact_id, dest_dir):
    run(["gh", "api", f"repos/{repo}/actions/artifacts/{artifact_id}/zip",
         "--header", "Accept: application/vnd.github+json",
         "-H", "X-GitHub-Api-Version: 2022-11-28"],
        **{"capture_output": False})  # won't work — need gh run download


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run-id", help="Specific GitHub Actions run ID (default: latest)")
    parser.add_argument("--repo", default=REPO)
    parser.add_argument("--dry-run", action="store_true",
                        help="Show what would be done without modifying md5refs files")
    parser.add_argument("--data-dir", metavar="DIR", action="append",
                        help="Pass --data-dir to update_md5refs.py for lossy verification")
    parser.add_argument("--no-verify", action="store_true",
                        help="Pass --no-verify to update_md5refs.py to skip lossy check")
    args = parser.parse_args()

    # Check gh is available
    try:
        run(["gh", "auth", "status"])
    except (subprocess.CalledProcessError, FileNotFoundError):
        sys.exit("gh CLI not found or not authenticated. Run: gh auth login")

    run_id = args.run_id or get_latest_run_id(args.repo)
    print(f"Using run ID: {run_id}")
    print(f"https://github.com/{args.repo}/actions/runs/{run_id}")

    with tempfile.TemporaryDirectory(prefix="grk_md5_") as tmpdir:
        tmp = Path(tmpdir)

        # Download all test-results artifacts for this run
        print("\nDownloading artifacts...")
        try:
            run(["gh", "run", "download", run_id,
                 "--repo", args.repo,
                 "--pattern", "test-results-*",
                 "--dir", str(tmp)])
        except subprocess.CalledProcessError as e:
            sys.exit(f"Failed to download artifacts:\n{e.stderr}")

        # Each artifact lands in tmp/<artifact-name>/
        for artifact_dir in sorted(tmp.iterdir()):
            if not artifact_dir.is_dir():
                continue
            name = artifact_dir.name

            # Parse artifact name; two historical formats:
            #   test-results-<os>-shared_<ON|OFF>   (new)
            #   test-results-<os>-<ON|OFF>           (old)
            prefix = "test-results-"
            if not name.startswith(prefix):
                continue
            rest = name[len(prefix):]  # e.g. "macos-latest-shared_ON" or "macos-latest-ON"

            if "-shared_" in rest:
                os_name, shared_part = rest.rsplit("-shared_", 1)
            elif rest.endswith("-ON") or rest.endswith("-OFF"):
                shared_part = rest.rsplit("-", 1)[1]
                os_name = rest[: -(len(shared_part) + 1)]
            else:
                print(f"  Skipping unrecognised artifact: {name}")
                continue
            shared_flag = shared_part  # ON or OFF

            platform_key = ARTIFACT_PLATFORM_MAP.get((os_name, shared_flag))
            if platform_key is None:
                print(f"  Skipping {name} (canonical Linux refs — update md5refs.txt manually if needed)")
                continue

            log_path = artifact_dir / "Testing" / "Temporary" / "LastTest.log"
            if not log_path.exists():
                print(f"  WARNING: no LastTest.log in {name}")
                continue

            print(f"\n--- {name} -> platform key: {platform_key} ---")
            cmd = [sys.executable, str(SCRIPT_DIR / "update_md5refs.py"),
                   "--platform", platform_key, str(log_path)]
            if args.data_dir:
                for d in args.data_dir:
                    cmd.extend(["--data-dir", d])
            if args.no_verify:
                cmd.append("--no-verify")
            if args.dry_run:
                print(f"  [dry-run] would run: {' '.join(cmd)}")
            else:
                result = subprocess.run(cmd, text=True)
                if result.returncode != 0:
                    print(f"  WARNING: update_md5refs.py exited {result.returncode}")

    if args.dry_run:
        print("\n[dry-run] No files were modified.")
    else:
        print("\nDone. Commit the updated md5refs-*.txt files:")
        for key in sorted(set(v for v in ARTIFACT_PLATFORM_MAP.values() if v)):
            ref = SCRIPT_DIR / f"md5refs-{key}.txt"
            if ref.exists():
                print(f"  git add {ref.relative_to(Path.cwd()) if ref.is_relative_to(Path.cwd()) else ref}")


if __name__ == "__main__":
    main()
