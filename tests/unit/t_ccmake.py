#!/usr/bin/env python3
"""Exercise automatic compiler setup through make with a local fake installer."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CompilerSetupTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'tools').mkdir()
        self.setup = self.root / 'tools/setup-cc.sh'
        self.setup.write_text('''#!/bin/sh
set -eu
[ "$1" = --build-dir ]
echo setup >> calls
mkdir -p "$2/cc/SmallerC"
for bin in smlrc smlrcc smlrpp; do
    printf '#!/bin/sh\\nexit 0\\n' > "$2/cc/SmallerC/$bin"
    chmod +x "$2/cc/SmallerC/$bin"
done
''')
        self.setup.chmod(0o755)
        (self.root / 'Makefile').write_text('''BUILD := output
NASM := nasm
include %s/apps/cc/Makefile.inc
.PHONY: first second
first second: cc-toolchain
\t@test -x $(CC_SC)/smlrpp
''' % ROOT)

    def make(self):
        return subprocess.run(['make', '-j4', 'first', 'second'], cwd=self.root,
                              capture_output=True, text=True)

    def test_missing_compiler_bootstraps_once_and_warm_build_reuses(self):
        for _ in range(2):
            result = self.make()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / 'calls').read_text(), 'setup\n')

    def test_missing_preprocessor_repairs_partial_install(self):
        result = self.make()
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.root / 'output/cc/SmallerC/smlrpp').unlink()
        result = self.make()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'calls').read_text(), 'setup\nsetup\n')

    def test_overlay_is_buildable_and_recovers_if_removed(self):
        with (self.root / 'Makefile').open('a') as makefile:
            makefile.write('\n$(eval $(call CC_PACKAGE,demo,demo,DEMO.OVL))\n')
        (self.root / 'output').mkdir()
        (self.root / 'output/demo.bin').write_bytes(b'resident-overlay')
        (self.root / 'tools/os88ovl.py').write_text(
            "import sys\nfrom pathlib import Path\n"
            "Path(sys.argv[sys.argv.index('-o') + 1]).write_bytes(b'overlay')\n"
            "Path(sys.argv[sys.argv.index('--trim') + 1]).write_bytes(b'resident')\n")
        (self.root / 'tools/os88pkg.py').write_text(
            "import sys\nfrom pathlib import Path\n"
            "Path(sys.argv[sys.argv.index('-o') + 1]).write_bytes(b'package')\n")
        overlay = self.root / 'output/DEMO.OVL'
        for _ in range(2):
            result = subprocess.run(['make', '-j4', '-o', 'output/demo.bin',
                                     'output/demo.o88', 'output/DEMO.OVL'],
                                    cwd=self.root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(overlay.read_bytes(), b'overlay')
            self.assertEqual((self.root / 'output/demo.o88').read_bytes(), b'package')
            overlay.unlink()

    def test_fresh_live_dependency_graph(self):
        result = subprocess.run(['make', '--dry-run', '-j4', 'live',
                                 'BUILD=' + str(self.root / 'fresh-live')],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('tools/setup-cc.sh --build-dir', result.stdout)
        self.assertIn('tools/getruncpm.py -o', result.stdout)
        self.assertNotIn('tools/getcpmsw.py -o', result.stdout)

    def test_setup_failure_stops_dependent_targets(self):
        self.setup.write_text('#!/bin/sh\necho setup-failed >&2\nexit 7\n')
        result = self.make()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('setup-failed', result.stderr)
        self.assertFalse((self.root / 'output/cc/SmallerC').exists())


if __name__ == '__main__':
    unittest.main()
