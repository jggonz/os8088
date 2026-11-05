#!/usr/bin/env python3
"""ARCADE's committed build state is the state of its inputs (ARCADE-SPEC 1.6).

    python3 tests/unit/t_arcprebuilt.py

apps/arcade/prebuilt/ holds the ROM-free parts `make arcade` builds the
package from, so no `make` and no release needs the ROMs or archive.org. They
were MADE from the ROMs by the translator, and STAMP records the hash of every
file that makes them. A change to the translator, a seed list, Q*bert's patch
or the set pins that is not followed by `make arcadeprebuilt` would ship the
OLD translation under the new source: this fails `make` then, by name, and
needs no ROM to say so. Whether the parts are the ones the ROMs make is the
soak row arcprebuilt's question, where the ROMs are.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'arcprebuilt.py'), 'stamp',
                    os.path.join(ROOT, 'apps', 'arcade', 'prebuilt')],
                   capture_output=True, text=True)
sys.stdout.write(r.stdout)
sys.stderr.write(r.stderr)
sys.exit(r.returncode)
