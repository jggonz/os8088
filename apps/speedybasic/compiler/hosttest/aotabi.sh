#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../../.." && pwd)
cd "$ROOT"
make --no-print-directory build/SPEEDYCC.RT
python3 apps/speedybasic/compiler/hosttest/aotabi.py
