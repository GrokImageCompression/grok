#!/usr/bin/env python3
# A/B sweep for the mercury decode path: decode every corpus file with the
# classic pipeline and with GRK_MERCURY=1, then compare outputs.
# Reversible streams must match bit-exactly. Irreversible streams may differ
# within a small peak tolerance: mercury computes 9/7 in f32 while classic
# uses int16 fixed point for low-precision images, so a few LSBs of
# divergence are by design (see mercury_fastpath.cpp).
#
# usage: mercury_ab_sweep.py <bin_dir> <work_dir> <corpus_dir> [corpus_dir...]

import concurrent.futures
import filecmp
import os
import subprocess
import sys

DECODE_TIMEOUT = 120
IRREVERSIBLE_PEAK_TOLERANCE = 4
EXTENSIONS = (".j2k", ".jp2", ".j2c", ".jhc", ".jph")
# these streams abort mid decode, so the pixels left behind depend on thread timing
THREAD_TIMING_DEPENDENT = {
    "security_threaded_decode_abort.j2k",
    "security_threaded_decode_abort_color.j2k",
}
# classic deliberately refuses a stream whose tile parts outnumber TNsot, because its
# parallel tile-part parsing needs TNsot to be right. mercury is tolerant, so the two
# legitimately diverge here rather than one of them being wrong.
TILE_PART_REJECTION = "must be less than number of tile parts"


def parse_codestream(path):
    """Return (numcomps, reversible) from SIZ/COD, or None if unparseable."""
    with open(path, "rb") as f:
        data = f.read(65536)
    jp2_codestream = data.find(b"\xff\x4f\xff\x51")
    if jp2_codestream < 0:
        return None
    data = data[jp2_codestream:]
    siz = data.find(b"\xff\x51")
    cod = data.find(b"\xff\x52")
    if siz < 0 or cod < 0 or cod + 14 > len(data):
        return None
    numcomps = int.from_bytes(data[siz + 38 : siz + 40], "big")
    reversible = data[cod + 13] == 1
    if numcomps == 0 or numcomps > 16384:
        return None
    return numcomps, reversible


def run_decode(bin_dir, in_file, out_file, mercury):
    env = dict(os.environ)
    if mercury:
        env["GRK_MERCURY"] = "1"
        env["GRK_MERCURY_DEBUG"] = "1"
    else:
        env["GRK_MERCURY"] = "0"
    try:
        r = subprocess.run(
            [os.path.join(bin_dir, "grk_decompress"), "-i", in_file, "-o", out_file],
            env=env,
            capture_output=True,
            text=True,
            timeout=DECODE_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        return None, "timeout", ""
    bailed = "mercury fastpath bail" in r.stderr
    # the bail note is on stderr but grok's logger writes to stdout, so hand back both
    return r.returncode, "bail" if bailed else "", r.stdout + r.stderr


def sweep_one(bin_dir, work_dir, in_file):
    """Returns (status, detail). status: ok/bail/skip/classic-rejects/unloadable/FAIL."""
    if os.path.basename(in_file) in THREAD_TIMING_DEPENDENT:
        return "skip", "output depends on thread timing"
    parsed = parse_codestream(in_file)
    if not parsed:
        return "skip", "unparseable header"
    numcomps, reversible = parsed
    base = os.path.join(work_dir, os.path.basename(in_file))
    classic_out = base + ".classic.tif"
    mercury_out = base + ".mercury.tif"

    classic_rc, _, classic_err = run_decode(bin_dir, in_file, classic_out, mercury=False)
    mercury_rc, note, mercury_log = run_decode(bin_dir, in_file, mercury_out, mercury=True)

    def judge():
        if classic_rc != 0 or not os.path.exists(classic_out):
            # classic can't decode it (corrupt stream or unsupported output);
            # mercury silently succeeding where classic errors would be a bug
            if classic_rc == 0 or mercury_rc == 0:
                if not os.path.exists(classic_out) and mercury_rc == classic_rc:
                    return "skip", "no tif output"
                if TILE_PART_REJECTION in classic_err:
                    return "classic-rejects", ""
                return "FAIL", f"exit codes diverge: classic {classic_rc} mercury {mercury_rc}"
            return "skip", "classic decode fails"
        if mercury_rc != 0 or not os.path.exists(mercury_out):
            return "FAIL", f"mercury decode failed (exit {mercury_rc}) where classic succeeded"

        if reversible:
            if not filecmp.cmp(classic_out, mercury_out, shallow=False):
                return "FAIL", "reversible outputs are not bit-exact"
            return ("bail", "") if note == "bail" else ("ok", "")

        if filecmp.cmp(classic_out, mercury_out, shallow=False):
            return ("bail", "") if note == "bail" else ("ok", "")

        # conformance mode (no -d): peak/MSE tolerances gate the comparison
        peaks = ":".join([str(IRREVERSIBLE_PEAK_TOLERANCE)] * numcomps)
        mses = ":".join([str(IRREVERSIBLE_PEAK_TOLERANCE)] * numcomps)

        def compare(base_file, test_file):
            r = subprocess.run(
                [os.path.join(bin_dir, "compare_images"), "-b", base_file, "-t", test_file,
                 "-n", str(numcomps), "-p", peaks, "-m", mses],
                capture_output=True,
                text=True,
                timeout=DECODE_TIMEOUT,
            )
            return r.returncode

        if compare(classic_out, mercury_out) != 0:
            # some classic outputs are tifs compare_images cannot read at all,
            # so blame the harness only when the file fails against itself
            if compare(classic_out, classic_out) != 0:
                return "unloadable", "compare_images cannot load classic output"
            return "FAIL", f"differs beyond peak tolerance {IRREVERSIBLE_PEAK_TOLERANCE}"
        return ("bail", "") if note == "bail" else ("ok", "")

    status, detail = judge()
    if status == "FAIL":
        with open(base + ".fail.txt", "w") as f:
            f.write(f"{detail}\n\nclassic (exit {classic_rc}):\n{classic_err}\n"
                    f"\nmercury (exit {mercury_rc}):\n{mercury_log}\n")
    else:
        for f in (classic_out, mercury_out):
            if os.path.exists(f):
                os.remove(f)
    return status, detail


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    bin_dir, work_dir = sys.argv[1], sys.argv[2]
    os.makedirs(work_dir, exist_ok=True)
    files = []
    for corpus in sys.argv[3:]:
        for name in sorted(os.listdir(corpus)):
            if name.lower().endswith(EXTENSIONS):
                files.append(os.path.join(corpus, name))
    if not files:
        print("no corpus files found")
        return 2

    counts = {"ok": 0, "bail": 0, "skip": 0, "classic-rejects": 0, "unloadable": 0, "FAIL": 0}
    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        futures = {pool.submit(sweep_one, bin_dir, work_dir, f): f for f in files}
        for fut in concurrent.futures.as_completed(futures):
            status, detail = fut.result()
            counts[status] += 1
            if status == "FAIL":
                failures.append(f"{os.path.basename(futures[fut])}: {detail}")

    print(f"mercury A/B sweep: {counts['ok']} compared ok, {counts['bail']} bailed to classic, "
          f"{counts['skip']} skipped, {counts['classic-rejects']} rejected by classic, "
          f"{counts['unloadable']} unloadable outputs, {counts['FAIL']} failed, of {len(files)} files")
    for f in sorted(failures):
        print("  FAIL", f)
    return 1 if counts["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
