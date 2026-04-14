#!/usr/bin/env python3
"""Simple expression calculator for the test-calc-py skill."""
import sys

if len(sys.argv) < 2:
    print("Usage: calc.py <expression>", file=sys.stderr)
    sys.exit(1)

expr = " ".join(sys.argv[1:])
try:
    result = eval(expr, {"__builtins__": {}}, {})
    print(result)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)
