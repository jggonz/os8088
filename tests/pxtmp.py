"""A row's OWN scratch path - in the run's tree, keyed to the process, swept at
exit (docs/WRITING-TESTS.md 5.4 and 5.5, both at once).

    import pxtmp
    corpus = pxtmp.private("pxcorpus", folder=True)   # .../build/pxcorpus-<pid>
    disk = pxtmp.private("pxdecode.img")              # .../build/pxdecode-<pid>.img

The PiXEL rows wrote their fixtures with a plain open("build/pxcorpus/...")
and handed the same spelling to `os88marty.scratch_disk`, which resolves it
into the run's frozen tree - so under a soak the files went into the live
checkout and the disk was built out of a directory nothing had written, and
six rows died on os88disk's "cannot read". `os88build.at()` is the half that
makes the writer and the reader agree; the pid is the half that keeps
`pxdraw` and `pxdrawvga`, which ran out of one `build/pxdrawf/`, from
rewriting each other's fixtures and disk while the other was reading them.

The answer is ABSOLUTE, so `scratch_disk`, `os88ui.boot` and a host-side read
of the image all name the same file without resolving it again.
"""
import atexit
import os
import shutil
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88build                                            # noqa: E402


def private(name, folder=False):
    """`build/<name>` made this process's own, resolved and swept at exit."""
    stem, ext = os.path.splitext(name)
    path = os.path.abspath(os88build.at("build/%s-%d%s"
                                        % (stem, os.getpid(), ext)))
    if folder:
        os.makedirs(path, exist_ok=True)
        atexit.register(shutil.rmtree, path, True)
    else:
        for p in (path, path + ".args"):        # scratch_disk's sidecar too
            atexit.register(_unlink, p)
    return path


def _unlink(p):
    try:
        os.unlink(p)
    except OSError:
        pass
