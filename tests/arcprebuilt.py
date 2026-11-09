#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcprebuilt.py - the committed parts are the ROMs' (ARCADE-
# SPEC 1.6): made again from the pinned ROMs (ARCFROM=rom), byte for byte the
# ones in apps/arcade/prebuilt/, and none holding a run of 8 of a ROM's bytes.
# Red when a part differs (the translator moved and nobody ran
# `make arcadeprebuilt`) or when a part carries ROM bytes.
# =============================================================================
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import arcprebuilt                  # noqa: E402
import os88build                    # noqa: E402

bdir = os88build.at('build')
subprocess.run(['make', '-s', 'ARCFROM=rom', 'BUILD=' + bdir] +
               [os.path.join(bdir, 'arcade', p) for p in arcprebuilt.PARTS], check=True)
sys.exit(arcprebuilt.compare(os.path.join(bdir, 'arcade'),
                             os.path.join(ROOT, 'apps', 'arcade', 'prebuilt'),
                             os.path.join(bdir, 'arcade-rom')))
