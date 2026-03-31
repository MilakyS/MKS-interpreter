import os
import subprocess
import time
import re
import sys
import json
import statistics

MKS_BIN = "./build/mks_run"
MKS_BENCH_DIR = "benchmarks/mks"
PY_BENCH_DIR = "benchmarks/python"
ITERATIONS = 3

def run_mks(filename):
    cmd = [MKS_BIN, filename]
    try:
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        stdout, stderr = process.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        process.kill()
        return -1, ("TIMEOUT", "0", "0", "0", "0", "0")

    m = re.search(r"Execution time: ([\d.]+) seconds", stderr)
    internal_time = float(m.group(1)) if m else -1

    # allocated=%zu threshold=%zu collections=%zu freed_objects=%zu freed_bytes=%zu
    gc_m = re.search(r"allocated=(\d+) threshold=(\d+) collections=(\d+) freed_objects=(\d+) freed_bytes=(\d+)", stderr)
    gc_stats = gc_m.groups() if gc_m else ("0", "0", "0", "0", "0", "0")

    return internal_time, gc_stats

def run_python(filename):
    cmd = ["python3", filename]
    start = time.time()
    try:
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        stdout, stderr = process.communicate(timeout=60)
        end = time.time()
    except subprocess.TimeoutExpired:
        process.kill()
        return -1
    return end - start

mks_files = [f for f in os.listdir(MKS_BENCH_DIR) if f.endswith(".mks")]
mks_files.sort()

results = []

header = f"{'Benchmark':<22} | {'MKS Avg (s)':<12} | {'Py Avg (s)':<12} | {'Ratio':<8} | {'GC Colls':<8} | {'Freed MB':<10} | {'Heap MB':<8}"
print(header)
print("-" * len(header))

for f in mks_files:
    name = f.replace(".mks", "")
    py_f = os.path.join(PY_BENCH_DIR, name + ".py")
    mks_f = os.path.join(MKS_BENCH_DIR, f)

    if not os.path.exists(py_f):
        continue

    mks_times = []
    py_times = []
    last_gc = None

    for i in range(ITERATIONS):
        mks_t, gc = run_mks(mks_f)
        if mks_t < 0:
            mks_times = []
            last_gc = gc
            break
        mks_times.append(mks_t)
        last_gc = gc

        py_t = run_python(py_f)
        if py_t < 0:
            py_times = []
            break
        py_times.append(py_t)

    if not mks_times or not py_times:
        msg = "TIMEOUT" if (last_gc and last_gc[0] == "TIMEOUT") else "ERROR"
        print(f"{name:<22} | {msg:<12} | {'-':<12} | {'-':<8} | {'-':<8} | {'-':<10} | {'-':<8}")
        continue

    avg_mks = statistics.mean(mks_times)
    avg_py = statistics.mean(py_times)
    ratio = avg_mks / avg_py if avg_py > 0 else 0

    freed_mb = int(last_gc[4]) / (1024 * 1024)
    heap_mb = int(last_gc[0]) / (1024 * 1024)

    print(f"{name:<22} | {avg_mks:<12.4f} | {avg_py:<12.4f} | {ratio:<8.2f} | {last_gc[2]:<8} | {freed_mb:<10.2f} | {heap_mb:<8.2f}")

    results.append({
        "name": name,
        "mks_avg": avg_mks,
        "py_avg": avg_py,
        "ratio": ratio,
        "gc_collections": int(last_gc[2]),
        "freed_bytes": int(last_gc[4]),
        "heap_bytes": int(last_gc[0])
    })

with open("benchmark_results.json", "w") as jf:
    json.dump(results, jf, indent=2)
