import re
import subprocess
import sys
from pathlib import Path

GROK_ROOT = Path(__file__).resolve().parent.parent
CMAKE_LISTS = GROK_ROOT / "CMakeLists.txt"
RELEASE_BRANCH = "master"
VERSION_LINE = re.compile(r"^  VERSION (\d+\.\d+\.\d+)$", re.MULTILINE)
VERSION_FORMAT = re.compile(r"^\d+\.\d+\.\d+$")


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(1)


def git(*arguments, check=True):
    result = subprocess.run(
        ["git", *arguments], cwd=GROK_ROOT, capture_output=True, text=True, check=False
    )
    if check and result.returncode != 0:
        fail(f"git {' '.join(arguments)} failed: {result.stderr.strip()}")
    return result


def next_patch_version(version):
    major, minor, patch = version.split(".")
    return f"{major}.{minor}.{int(patch) + 1}"


def main():
    if len(sys.argv) > 2:
        fail(
            "usage: bump_version_and_tag.py [X.Y.Z], defaults to the next patch version"
        )

    if git("branch", "--show-current").stdout.strip() != RELEASE_BRANCH:
        fail(f"not on {RELEASE_BRANCH}")
    if git("status", "--porcelain", "--untracked-files=no").stdout.strip():
        fail("tracked files have uncommitted changes")
    git("fetch", "-q", "origin", RELEASE_BRANCH)
    head = git("rev-parse", "HEAD").stdout.strip()
    remote_head = git("rev-parse", f"origin/{RELEASE_BRANCH}").stdout.strip()
    if head != remote_head:
        fail(
            f"{RELEASE_BRANCH} differs from origin/{RELEASE_BRANCH}, push or pull first"
        )

    # newline="" keeps the file's own line endings on Windows
    with open(CMAKE_LISTS, newline="") as cmake_file:
        cmake_text = cmake_file.read()
    matches = VERSION_LINE.findall(cmake_text.replace("\r\n", "\n"))
    if len(matches) != 1:
        fail("expected exactly one '  VERSION X.Y.Z' line in CMakeLists.txt")
    current_version = matches[0]

    new_version = (
        sys.argv[1] if len(sys.argv) == 2 else next_patch_version(current_version)
    )
    if not VERSION_FORMAT.match(new_version):
        fail(f"version must be X.Y.Z, got {new_version}")
    tag = f"v{new_version}"
    if (
        git("rev-parse", "-q", "--verify", f"refs/tags/{tag}", check=False).returncode
        == 0
    ):
        fail(f"tag {tag} already exists locally")
    if git("ls-remote", "--tags", "origin", f"refs/tags/{tag}").stdout.strip():
        fail(f"tag {tag} already exists on origin")

    answer = input(
        f"bump {current_version} to {new_version}, tag {tag} and push? [y/N] "
    )
    if answer.strip() != "y":
        fail("stopped")

    CMAKE_LISTS.write_text(
        cmake_text.replace(
            f"  VERSION {current_version}", f"  VERSION {new_version}", 1
        ),
        newline="",
    )
    git("commit", "-q", "-m", f"bump version to {new_version}", "CMakeLists.txt")
    git("tag", "-a", tag, "-m", tag)
    git("push", "origin", RELEASE_BRANCH)
    git("push", "origin", tag)
    print(f"pushed {tag}, the build workflow uploads the release archives")


if __name__ == "__main__":
    main()
