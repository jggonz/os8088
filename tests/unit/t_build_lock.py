#!/usr/bin/env python3
"""A private build tree stays locked while a process reads its products."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

TOOLS = str(Path(__file__).resolve().parents[2] / 'tools')
HOLDER = """
import sys
sys.path.insert(0, sys.argv[1])
from os88build import _Lock
with _Lock(sys.argv[2]):
    pass
with _Lock(sys.argv[2]):
    pass
print('retained and reentered', flush=True)
sys.stdin.readline()
"""
WAITER = """
import sys
sys.path.insert(0, sys.argv[1])
from os88build import _Lock
with _Lock(sys.argv[2]):
    print('acquired', flush=True)
"""


class BuildLock(unittest.TestCase):
    def test_read_hold_blocks_another_builder_until_process_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, 'tree.lock')
            holder = subprocess.Popen([sys.executable, '-c', HOLDER, TOOLS, path],
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True)
            waiter = None
            try:
                self.assertEqual(holder.stdout.readline().strip(), 'retained and reentered')
                waiter = subprocess.Popen([sys.executable, '-c', WAITER, TOOLS, path],
                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                          text=True)
                with self.assertRaises(subprocess.TimeoutExpired):
                    waiter.communicate(timeout=.5)
                holder.communicate('\n', timeout=5)
                self.assertEqual(holder.returncode, 0)
                output, errors = waiter.communicate(timeout=5)
                self.assertEqual(waiter.returncode, 0, errors)
                self.assertEqual(output.strip(), 'acquired')
            finally:
                for process in (waiter, holder):
                    if process is not None and process.poll() is None:
                        process.kill()
                        process.communicate()


if __name__ == '__main__':
    unittest.main()
