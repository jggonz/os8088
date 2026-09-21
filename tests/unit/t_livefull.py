#!/usr/bin/env python3
"""t_livefull: EVERYTHING THIS PROJECT BUILDS IS ON THE LIVE MEDIA (SPEC.md 80.6).

    python3 tests/unit/t_livefull.py

The live USB image and the live CD are the one artefact whose premise is
COMPLETENESS. Every floppy in this tree is a curation - 354 clusters is the
geometry that runs out first and this project keeps making applications, so
each disk is a decision about what comes off. The live volume is 16,324
clusters of 2,048 bytes, it is on demand, and it is what a release page offers
to somebody who wants the machine rather than a floppy. A program missing from
it is missing from the release.

THAT PREMISE HAD NOTHING HOLDING IT and it had already failed four ways:

  THEWIRE.O88   absent, and not by a decision. It is a SYSAPPS package that
                the desktop zone launches BY NAME out of the BOOT volume's
                SYSTEM/ (SPEC.md 26.7), so an apps FLOPPY rightly leaves it
                off - and the live media took the floppy's answer although it
                IS the boot volume. Every live image ever cut booted to a
                desktop whose Wire zone opened nothing.
  four packages RECORDER, HELLO, PACMAN and SCRIBE are each built by `all` and
                carried by no floppy, every one of them on a cluster argument
                that is false at 32MB.
  the stories   FROTZ.O88 rode APPS/ with nothing to play, because the library
                is fetched rather than committed - on a target that already
                acquires two other fetches.
  both fills    the RunCPM master disk and the CP/M software collection were
                priced in 1.44MB-floppy clusters, so the volume carried 62 of
                77 master-disk files with a LEFT-OFF.TXT naming the other
                fifteen, and no CP/M software at all.

None of the four was caught by anything, because nothing in the tree had ever
read that image. Each half below catches a different one of them, and the
halves are not interchangeable:

  PART A  reads the payload list the BUILD wrote down (build/livepayload.txt,
          which `all` emits from $(LIVEARGS) itself) and needs no image. It is
          what fails when somebody adds apps/newthing/ and does not put it on
          the live media - the case the owner asked for - so it runs in the
          FAST tier on every build, where the image does not exist. It reads an
          ARTEFACT rather than shelling out to make, and that is not a
          convenience: with a knob in the environment $(VIDSTAMP)'s rule
          deletes build/kernel.bin, so a row that ran make would rewrite build/
          under whatever is running beside it (tests/unit/t_registry refuses
          one that does, which is how this was caught).
  PART B  walks build/os8088-usb.img itself, and runs only when `make usb` has
          built one. A list can name a file that never lands (a folder the
          recipe forgot, a --select that truncated); the variable is right and
          the image is wrong, which is PART A's blind spot exactly.

THE EXEMPTIONS ARE A LIST WITH A REASON EACH, and that is the shape on
purpose: `filter-out` of a name is how this tree already says "this one is
deliberate" (see $(CORE_SYSONLY) in the Makefile). A package leaves the live
media by being written down here, which cannot be done by accident, and the
rule is not weakened for anything else.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
USBIMG = os.path.join(ROOT, "build", "os8088-usb.img")
PAYLOAD = os.path.join(ROOT, "build", "livepayload.txt")

# apps/ directories that are NOT a shipping package, each with the reason it
# is not one. Anything else under apps/ must reach the live media.
EXEMPT_DIRS = {
    "cc": "the C SDK (SPEC.md 73) - crt0, the thunks and ccsmoke, which is a "
          "gate rather than a program; the packages built WITH it are "
          "cword/, paccman/, runcpm/, c64/, apple2/, weave/ and loom/, and "
          "every one of those is checked below",
    "fptest": "a capability gate, built by `make bench` and shipped on no "
              "floppy (tests/ is where the non-shipping packages live; this "
              "one predates that folder)",
    "imgtest": "a capability gate, as fptest - `all` names build/imgtest.o88 "
               "only to keep it assembling",
    "wire": "WIREFRAME (SPEC.md 78.9) is an INSTRUMENT and not an "
            "application: it is the bench for 78.5's draw orders, `make "
            "wiredisk` builds its disk, and `all` names it only so that it "
            "keeps compiling. The one entry here that is a decision about "
            "the program rather than about the folder",
}

# apps/<dir> -> the 8.3 name its package lands under. The rule is mechanical -
# uppercase, first eight characters - and `solitaire` is the only directory in
# the tree that the truncation actually moves. Spelt as a function rather than
# a table so a ninth-character name cannot be added without it holding.
def pkg_name(d):
    return d[:8].upper() + ".O88"


_PAYLOAD = None


def mk(var):
    """One payload variable, as the BUILD wrote it into
    build/livepayload.txt - `KEY value`, one entry a line.

    NOT `make print-VAR`, although that target exists and is what a person
    types. A gate may not shell out to make: with any knob in the environment
    $(VIDSTAMP)'s rule DELETES build/kernel.bin and every boot sector, so a
    row that ran make would rewrite build/ under whatever is running beside
    it. tests/unit/t_registry refuses such a row by name. Reading the artefact
    the build already emitted is the same answer with none of that."""
    global _PAYLOAD
    if _PAYLOAD is None:
        _PAYLOAD = {}
        if os.path.exists(PAYLOAD):
            with open(PAYLOAD) as fh:
                for line in fh:
                    parts = line.split()
                    if len(parts) == 2:
                        _PAYLOAD.setdefault(parts[0], []).append(parts[1])
    return _PAYLOAD.get(var)


# --- the live image, walked from the FAT16 format ---------------------------
# Deliberately not os88disk's own reader, for tests/unit/t_image.py's reason:
# a check that reads the image through the code that wrote it agrees with
# itself whatever either of them does.
class Hdd:
    def __init__(self, blob):
        self.blob = blob
        base = struct.unpack_from("<I", blob, 446 + 8)[0] * 512
        self.base = base
        (self.byts, self.spc, rsvd, nfat, self.rootent, tot16, _,
         fatsz) = struct.unpack_from("<HBHBHHBH", blob, base + 11)
        tot32 = struct.unpack_from("<I", blob, base + 32)[0]
        self.tot = tot16 or tot32
        self.fat0 = base + rsvd * self.byts
        self.rootlba = rsvd + nfat * fatsz
        self.rootsecs = (self.rootent * 32 + self.byts - 1) // self.byts
        self.data0 = self.rootlba + self.rootsecs
        self.nclus = (self.tot - self.data0) // self.spc

    def fat(self, n):
        return struct.unpack_from("<H", self.blob, self.fat0 + n * 2)[0]

    def chain(self, c):
        out = []
        while 2 <= c < 0xFFF0 and len(out) <= self.nclus + 2:
            out.append(c)
            c = self.fat(c)
        return out

    def walk(self, lba=None, secs=None, path=""):
        """(full path, size) for every file, folders as 'PATH/'."""
        if lba is None:
            lba, secs = self.rootlba, self.rootsecs
        off = self.base + lba * self.byts
        for i in range(secs * self.byts // 32):
            e = self.blob[off + i * 32: off + i * 32 + 32]
            if not e or e[0] == 0x00:
                break
            if e[0] == 0xE5 or (e[11] & 0x0F) == 0x0F or (e[11] & 0x08):
                continue
            nm = e[0:11].decode("ascii", "replace")
            stem, ext = nm[:8].strip(), nm[8:].strip()
            name = stem + ("." + ext if ext else "")
            clus = struct.unpack_from("<H", e, 26)[0]
            size = struct.unpack_from("<I", e, 28)[0]
            if e[11] & 0x10:
                if e[0:1] == b".":
                    continue
                yield (path + name + "/", 0)
                for c in self.chain(clus):
                    for row in self.walk(self.data0 + (c - 2) * self.spc,
                                         self.spc, path + stem + "/"):
                        yield row
            else:
                yield (path + name, size)


def main():
    # ==================================================================
    # PART A - the PAYLOAD LISTS, asked of make. No image needed, so this
    # is the half that runs on every build.
    # ==================================================================
    check(os.path.exists(PAYLOAD),
          "no %s - the build did not write the live payload manifest, so "
          "PART A has nothing to check. It is in `all` and costs a printf; "
          "run `make` (SPEC.md 80.6)" % os.path.relpath(PAYLOAD, ROOT))
    live = mk("LIVEARGS")
    check(live,
          "build/livepayload.txt names no LIVEARGS - the live payload list is "
          "gone or renamed, and every check below is vacuous")
    if not live:
        return done("livefull")

    # Every argument is [FOLDER:]path; what is checked is the BASENAME, which
    # is the 8.3 name os88disk.py derives and therefore the name on the image.
    named = {os.path.basename(a.split(":")[-1]).upper() for a in live}

    appdirs = sorted(d for d in os.listdir(os.path.join(ROOT, "apps"))
                     if os.path.isdir(os.path.join(ROOT, "apps", d)))
    check(len(appdirs) > 30,
          "only %d directories under apps/ - that is not this tree, and a "
          "gate that found none would pass" % len(appdirs))

    for d in appdirs:
        if d in EXEMPT_DIRS:
            continue
        check(pkg_name(d) in named,
              "apps/%s/ builds a package and the live media does not carry "
              "it: %s is in no list that reaches $(LIVEARGS). Put it on the "
              "everything disk ($(ALLAPPSARGS)) or, if it is deliberately "
              "not a shipping program, add it to EXEMPT_DIRS in %s with the "
              "reason - SPEC.md 80.6 says the live volume is the one image "
              "whose premise is completeness"
              % (d, pkg_name(d), os.path.relpath(__file__, ROOT)))

    # ...and the guard on the guard, which is $(CORE_SYSONLY)'s arrangement:
    # an exemption for a directory that is not there any more excuses nothing
    # and is how an exception list rots.
    for d in EXEMPT_DIRS:
        check(d in appdirs,
              "EXEMPT_DIRS names apps/%s/, which does not exist - an "
              "exemption for a deleted package is a line that reads like a "
              "decision and holds nothing" % d)

    # THE WIRE by name, because its absence was a BUG and not a missing list
    # entry: $(ALLAPPSARGS) carries $(APPSYS) (the Task Manager alone, which
    # is right for a floppy) and the live volume IS the boot volume.
    check("THEWIRE.O88" in named,
          "THEWIRE.O88 is not in $(LIVEARGS). The live media is ONE VOLUME - "
          "it is the boot volume - and the desktop zone launches the Wire by "
          "name out of the boot volume's SYSTEM/ (SPEC.md 26.7), so without "
          "it the machine boots to a desktop whose Wire zone opens nothing. "
          "$(LIVESYSARGS) is what carries it")

    # The four that ride no floppy at all (SPEC.md 19.10.1). Named here rather
    # than derived because the apps/ sweep above already covers them by
    # folder - this says so a second time in the words a reader would search
    # for, and fails with the sentence that explains the cluster argument.
    for name, why in (
            ("RECORDER.O88", "SPEC.md 35.1 - off the apps disks, built by "
                             "`all`"),
            ("HELLO.O88", "SPEC.md 27.0 - the SDK's worked example, off "
                          "every floppy"),
            ("PACMAN.O88", "SPEC.md 89 - off the disk lists while DOT "
                           "DELIRIUM wanted its six 360KB clusters"),
            ("SCRIBE.O88", "SPEC.md 95 - off the apps disk because two word "
                           "processors are 49KB of one floppy")):
        check(name in named,
              "%s is on no floppy in this tree (%s) and is now not on the "
              "live media either, so it ships nowhere. Every one of those "
              "exclusions is a 354-cluster argument and the live volume has "
              "~26MB free" % (name, why))

    # SCRIBE needs its FOLDER to be whole: it resolves SCRIBE.OVL in the
    # launching instance's directory (SPEC.md 19.2.1), so the package without
    # the overlay is a program whose every menu then refuses.
    for name in ("SCRIBE.OVL", "SCWELCOM.RTF"):
        check(name in named,
              "SCRIBE.O88 is on the live media and %s is not. The overlay is "
              "resolved in the LAUNCHING instance's directory, so SCRIBE/ "
              "has to be a whole program or it is a menu bar that refuses" % name)

    # The documents MEDIA/ is the default File Open location for (SPEC.md
    # 38.10). $(MEDIA_EXTRA) is derived in the Makefile from the category
    # disks' own list, so this checks that the derivation still yields
    # something and that it reached the live payload.
    extra = mk("MEDIA_EXTRA")
    check(extra,
          "$(MEDIA_EXTRA) is empty in build/livepayload.txt - Sheet, Chart, ArtfulType and Paint ship "
          "on the live media with nothing in the folder their Open dialog "
          "starts on")
    for path in extra or []:
        check(os.path.basename(path).upper() in named,
              "%s is in $(MEDIA_EXTRA) and not in $(LIVEARGS)"
              % os.path.basename(path))

    # ==================================================================
    # PART B - the IMAGE. Only when one has been built: `make usb` needs
    # the C toolchain and three fetches, so a plain clone has none.
    # ==================================================================
    if not os.path.exists(USBIMG):
        print("t_livefull: no %s - PART B skipped (`make usb`)"
              % os.path.relpath(USBIMG, ROOT))
        return done("livefull")

    with open(USBIMG, "rb") as fh:
        vol = Hdd(fh.read())
    rows = list(vol.walk())
    onimg = {p.upper() for p, _ in rows}
    bybase = {os.path.basename(p.rstrip("/")).upper() for p, _ in rows}

    check(len(rows) > 300,
          "the live image holds %d entries - it was 154 before SPEC.md 80.6 "
          "and is ~420 after; a number near the first means the payload "
          "silently shrank back" % len(rows))

    # every name the list promised actually landed
    for name in sorted(named):
        check(name in bybase,
              "$(LIVEARGS) names %s and build/os8088-usb.img does not carry "
              "it - the list is right and the image is wrong, which is the "
              "case PART A cannot see" % name)

    # THE FETCHED PAYLOADS, each against its own manifest rather than against
    # a count. These are the three that a floppy-priced --select truncates,
    # and a truncation reads exactly like a working disk.
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    try:
        import getstories                                          # noqa: E402
    except Exception as exc:                                       # pragma: no cover
        getstories = None
        check(False, "cannot import tools/getstories.py: %s" % exc)
    if getstories:
        for st in getstories.MANIFEST:
            check(("STORIES/%s/%s" % (st.folder, st.name)).upper() in onimg,
                  "%s/%s is in getstories' MANIFEST and not on the live "
                  "media. The whole library is 2,519KB - more than any floppy "
                  "holds, which is why the Makefile cuts it per geometry - "
                  "and this volume has ~26MB free, where a cut is a decision "
                  "nobody took" % (st.folder, st.name))

    try:
        import getcpmsw                                            # noqa: E402
    except Exception as exc:                                       # pragma: no cover
        getcpmsw = None
        check(False, "cannot import tools/getcpmsw.py: %s" % exc)
    if getcpmsw:
        for area, disk, what, _, _ in getcpmsw.AREAS:
            pre = ("RUNCPM/" + disk + "/").upper()
            check(any(p.upper().startswith(pre) and not p.endswith("/")
                      for p, _ in rows),
                  "the CP/M software collection's %s area (%s) lands in "
                  "RUNCPM/%s and the live media has nothing there. "
                  "getcpmsw.py's \"hdd\" policy carries every area - a "
                  "floppy's policy does not, and this volume is not a floppy"
                  % (area, what, disk))

    # ...and RunCPM's own master disk, WHOLE. The list the fetch wrote is the
    # manifest; the image must hold all of it, because the only reason a file
    # is missing from a 32MB volume is a selection priced for a floppy.
    a0list = os.path.join(ROOT, "build", "runcpm-disk", "A0.list")
    if os.path.exists(a0list):
        with open(a0list) as fh:
            want = [ln.split()[0] for ln in fh if ln.split()]
        missing = [b for b in want
                   if ("RUNCPM/A/0/" + b).upper() not in onimg]
        check(not missing,
              "the live media carries %d of RunCPM's %d master-disk files - "
              "missing %s. --select \"hdd\" carries all of them; --select "
              "1440 does not, and that is what this image was built with "
              "until SPEC.md 80.6"
              % (len(want) - len(missing), len(want), ", ".join(missing[:6])))

    # A LEFT-OFF.TXT is honest on a floppy and a lie here: on 26MB free the
    # only files left off are the three above the 65,535-byte record limit.
    note = [p for p, _ in rows if p.upper().endswith("A/0/LEFT-OFF.TXT")]
    check(len(note) == 1,
          "RUNCPM/A/0/LEFT-OFF.TXT is not on the live media - it is what "
          "tells a CP/M session which master-disk files this volume cannot "
          "open, and three of them it genuinely cannot")

    # the volume still has room to be used, which is the point of a stick
    used = sum(1 for c in range(2, vol.nclus + 2) if vol.fat(c) != 0)
    freekb = (vol.nclus - used) * vol.spc * vol.byts // 1024
    check(freekb > 8192,
          "the live volume has %dKB free. It is a WRITABLE stick (SPEC.md "
          "80.3) and a user's own documents go on it; a payload that fills "
          "it is a payload to decide about, not to discover" % freekb)
    print("t_livefull: %d entries, %d of %d clusters, %dKB free"
          % (len(rows), used, vol.nclus, freekb))

    return done("livefull")


if __name__ == "__main__":
    sys.exit(main())
