"""Repeat the published Linux benchmark using separately linked executables."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("unchanged", type=Path, help="unchanged-build benchmark executable")
parser.add_argument("patched", type=Path, help="patched-build benchmark executable")
parser.add_argument("--cpu", type=int, default=2, help="Linux CPU affinity (default: 2)")
parser.add_argument("--iterations", type=int, default=4800)
parser.add_argument("--trials", type=int, default=9, help="number of alternating pairs")
parser.add_argument("--output", type=Path, default=HERE / "benchmark-reproduced.json")
args = parser.parse_args()
if not 1 <= args.iterations <= 1000000 or args.trials < 1:
    parser.error("iterations must be 1..1000000 and trials must be positive")
executables = {"unchanged": args.unchanged.resolve(), "patched": args.patched.resolve()}
expected = (HERE / "unchanged.pgm").read_bytes()
samples = []

with tempfile.TemporaryDirectory(prefix="gray-chroma-benchmark-") as scratch:
    for trial in range(args.trials):
        order = ["unchanged", "patched"] if trial % 2 == 0 else ["patched", "unchanged"]
        pair = {"trial": trial + 1, "order": order, "measurements": {}}
        for build in order:
            output = Path(scratch) / f"{build}.pgm"
            command = ["taskset", "-c", str(args.cpu), str(executables[build]),
                       str(HERE / "source.rgb0"), str(args.iterations), str(output)]
            result = subprocess.run(command, capture_output=True, text=True, check=True)
            if result.stderr:
                raise RuntimeError(result.stderr)
            value = json.loads(result.stdout)
            if output.read_bytes() != expected:
                raise RuntimeError(f"{build}: output differs from the published example; "
                                   "this runner expects the documented source/build pair")
            pair["measurements"][build] = value
            print(f"Trial {trial + 1}/{args.trials} {build}: "
                  f"{value['us_per_frame']:.3f} us/frame", flush=True)
        a, b = [pair["measurements"][build]["us_per_frame"]
                for build in ["unchanged", "patched"]]
        pair["time_reduction_percent"] = 100 * (1 - b / a)
        samples.append(pair)

values = {build: [s["measurements"][build]["us_per_frame"] for s in samples]
          for build in ["unchanged", "patched"]}
medians = {build: statistics.median(v) for build, v in values.items()}
summary = {
    "median_us_per_frame": medians,
    "sample_range_us_per_frame": {b: [min(v), max(v)] for b, v in values.items()},
    "time_reduction_percent": 100 * (1 - medians["patched"] / medians["unchanged"]),
    "throughput_increase_percent": 100 * (medians["unchanged"] / medians["patched"] - 1),
    "median_paired_time_reduction_percent": statistics.median(s["time_reduction_percent"] for s in samples),
    "paired_time_reduction_range_percent": [min(s["time_reduction_percent"] for s in samples),
                                             max(s["time_reduction_percent"] for s in samples)],
    "all_outputs_equal_to_displayed_example": True,
}
def read_optional(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return "unavailable"

report = {
    "affinity_cpu": args.cpu,
    "cpu_governor": read_optional(f"/sys/devices/system/cpu/cpu{args.cpu}/cpufreq/scaling_governor"),
    "energy_performance_preference": read_optional(f"/sys/devices/system/cpu/cpu{args.cpu}/cpufreq/energy_performance_preference"),
    "method": "alternating paired runs; 500 warmup conversions per process; single thread; native CPU dispatch; repeated same in-memory input; only sws_scale timed; no callback instrumentation; allocation, setup, file I/O and output checking excluded",
    "conversion": "RGB0 -> GRAY8; 512x320; BT.709; full source/destination range; SWS_BILINEAR",
    "iterations_per_sample": args.iterations,
    "source_rgb0_sha256": hashlib.sha256((HERE / "source.rgb0").read_bytes()).hexdigest(),
    "executable_sha256": {b: hashlib.sha256(p.read_bytes()).hexdigest() for b, p in executables.items()},
    "samples": samples,
    "summary": summary,
}
args.output.write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(summary, indent=2))
