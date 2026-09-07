#!/usr/bin/env python3
"""Exercise only argument rejection. Never request backend or firmware access."""
import subprocess
import sys

executable = sys.argv[1]
cases = [[], ["--help"], ["apply"], ["apply", "DISCRETE"],
         ["apply", "discrete", "--allow-unvalidated-bios"], ["diagnose", "extra"],
         ["/bin/sh"], ["apply", "discrete; reboot"], ["apply", "--debug"],
         ["diagnose", "--json"], ["apply", ""], ["apply", "../discrete"]]
for args in cases:
    result = subprocess.run([executable, *args], capture_output=True, timeout=5)
    assert result.returncode == 64, (args, result)
    assert not result.stdout, (args, "unexpected stdout")
    assert b"expected diagnose" in result.stderr, (args, result.stderr)
print(f"Passed {len(cases)} argument-rejection cases; no backend invoked.")
