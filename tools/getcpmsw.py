#!/usr/bin/env python3
"""getcpmsw: fetch the CP/M GAMES AND APPLICATIONS the RUNCPM floppies carry
beside RunCPM's master disk (SPEC.md §74.6).

    python3 tools/getcpmsw.py -o build/cpmsw               # fetch + verify
    python3 tools/getcpmsw.py -o build/cpmsw --check       # verify, never fetch
    python3 tools/getcpmsw.py -o build/cpmsw --list        # what is shipped
    python3 tools/getcpmsw.py -o build/cpmsw --select 1440  # the areas that
                              # geometry carries, one `<DISK AREA>:<path>`
                              # line per file, ready for os88disk.py
    python3 tools/getcpmsw.py -o build/cpmsw --cost 1440    # ...what that
                              # selection costs, in that geometry's clusters
    python3 tools/getcpmsw.py -o build/cpmsw --slots 1440   # ...and the
                              # os88disk.py --dir-slots flags its areas need
    python3 tools/getcpmsw.py -o build/cpmsw --from DIR    # take the files
                              # from a local copy of the collection instead
    python3 tools/getcpmsw.py --refresh -o build/cpmsw     # re-read the
                              # collection and print a new PINNED table

**Nothing this script downloads is committed** (CONTRIBUTING.md §6, and the
same decision `tools/getstories.py` and `tools/getruncpm.py` took): the games
are their own authors' work, a git repository is not a distribution channel
for them, and the bytes land in `build/`, which is ignored outright. What IS
committed is the PIN — a Google Drive file id, a SHA-256 and a size per file —
so the floppies rebuild byte-for-byte (`tools/os88disk.py` pins the volume
serial and every FAT timestamp; this pins the input) and a collection that
moved under us is a hard failure rather than a silent difference.

WHERE THEY COME FROM: the public **RunCPM software collection** on Google
Drive — the A..P/0..F drive tree RunCPM users share, the same shape RunCPM
itself reads (`A/0` is the master disk `tools/getruncpm.py` fetches). Its
coordinates are the key of everything fetched here, so a file in
`build/cpmsw/` is the file there and `--from` a local copy of the collection
works; where each lands ON THE FLOPPY is `AREAS` below, and all of it lands
in a user area of drive A (the reason is there). What is taken:

    A/5   Yahoo Software's arcade games — LADDER, CATCHUM, PM — with the
          LADCONF/CATCONF terminal setup they ship with, and the area ranked
          first. **Tell them they are on `10) Heathkit/Zenith H19 (ANSI)`**:
          that entry emits plain 1-based `ESC[row;colH`, which is §74.2's
          subset, while their own `6) DEC VT-100 (ANSI)` entry emits those
          coordinates BIASED BY 32 (`ESC[54;32f` for row 22, column 0) and
          wraps its status panel on a real VT-100 exactly as it does here -
          measured against upstream RunCPM through a VT-100 model, so it is
          the game's encoding and not this terminal's (SPEC.md 71.6).
    N/0   Nemesis, Dungeon Master and Castle — dungeon crawlers with their
          level and monster data (the two PDF manuals in that area, 5MB and
          12MB, are not fetched: documentation, and orders of magnitude
          past anything a floppy holds).
    G/4   GAINA, a video game with its board overlays.
    H/3   Borland's Turbo Pascal v3.01A, which needs no telling: it comes up
          `Terminal: ANSI`.
    D/0   MicroPro WordStar v3.30 (`WINSTALL` picks the terminal).

    ...and four more the areas below name and no geometry has room for -
    ZDE and PMARC, LU and NULU, Super Directory, BBC BASIC - fetched and
    pinned all the same, so that changing one line of POLICY ships them.

WHAT IS NOT HERE, and cannot be: the collection also carries Zork, The
Hitchhiker's Guide to the Galaxy and Colossal Cave Adventure, and **this port
could not run them if it shipped them** — `ZORK1.DAT` is 76,288 bytes,
`HITCH.DAT` 113,330 and Adventure's `ADVT.DAT` 68,864, and §74.3's record
model reads a file WHOLE through a 16-bit count, so nothing above 65,535
bytes can be opened. The size limit is checked here (`MAX_FILE`), on every
file, so a refreshed collection cannot quietly put an unopenable file on a
disk. Infocom's two are also Activision's to give away and they have not,
which is `tools/getstories.py`'s paragraph and the same answer.

--select answers a geometry's areas from POLICY below — **a decision, not a
leftover, and an area at a time**: a game without its data files is a game
that does not start, so an area is carried whole or not at all. The games are
priced FIRST and the master disk fills what is left (`--cost` is what the
Makefile hands `getruncpm.py --select --reserve-clusters`), which is why the
720KB disk keeps every program of the master disk and loses only its sources:
the same curation the 360KB disk already made, made by arithmetic. Whatever a
geometry leaves off is NAMED ON THAT DISK, in `GAMES.TXT` beside the master
disk — what is there, the line to type at `A>` to reach it, the terminal to
tell it, and what is only on the 1.44MB disk or on no disk here.
"""
import argparse
import hashlib
import html
import http.cookiejar
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

TIMEOUT = 180
MAX_FILE = 65535          # a whole file must fit a 16-bit count (SPEC.md §74.3)
DIR_ENTRY = 32            # a FAT directory entry
# spare directory slots every game area ships with: the kernel does not grow a
# directory (SPEC.md §18.5) and these games SAVE - Nemesis writes a character
# file (`ARAGORN.CHR` is one that came with it), LADDER and CATCHUM rewrite
# their score file, TERMDEF writes `TERM.DEF`. A rewritten file needs no slot;
# a new one does, and without spare slots the first save that makes a file
# fails 'Not saved' the way wave 4's 360KB disk did (SPEC.md §74.5).
SPARE_SLOTS = 16

# What each geometry carries (SPEC.md §74.6). A DECISION per geometry, in the
# shape SPEC.md §74.5's CURATED already has: 1.44MB has room for all three
# areas beside the whole master disk; the 1.2MB disk carries FOUR of the five
# and drops the largest, H/3 (WordStar, 519 clusters), which is a DECISION and
# not the fill's leftovers - its clusters are 512 bytes like the 1.44MB one's
# rather than 1,024 like the two DD disks', but it has 2,371 of them against
# 2,847, and all five areas cost 1,997. Taking all five leaves A/0 136
# clusters, and the ranked fill then stops halfway through the .COMs at
# SUBMITD - no MBASIC, no PIP, no STAT, no TE, no Z80ASM, no ZEXDOC. That is
# the alphabetical-.COM-fill failure the docstring above warns about, arrived
# at from the other end, and it is why an area comes off rather than the
# programs: at four areas the games are 1,473 clusters, A/0 gets 660 and the
# fill reaches past every .COM into the documentation and the first sources -
# 63 of the 77, which is MORE master disk than the 1.44MB disk holds. Turbo
# Pascal (D/0) stays and WordStar goes because the choice is by SIZE, and
# GAMES.TXT names it under the heading that says it is on the 1.44MB disk;
# the 720KB disk carries the arcade area
# and pays for it out of the master disk's SOURCES, which is the trade the
# 360KB disk already makes for room to save; the 360KB disk carries no games
# at all - 297 clusters cannot hold 206KB of arcade AND the programs, and a
# disk that dropped the programs for games would not be RunCPM's disk any
# more. GAMES.TXT says so on the disk itself.
POLICY = {1440: ["A/5", "N/0", "G/4", "D/0", "H/3"],
          1200: ["A/5", "N/0", "G/4", "D/0"],
          720: ["A/5"],
          360: []}

# The public RunCPM software collection on Google Drive: the folder that holds
# the A..P drive tree. Only --refresh reads it as a FOLDER; an ordinary fetch
# goes straight to the pinned file ids below, so a build makes one request per
# file and none per folder.
COLLECTION = "1FZAcIP9_Hf0lCL_FDs1qJCrk2GHdirv8"
# ...and the folder id of each area --refresh re-reads (the collection's own
# folders; a drive letter's folder holds its user areas)
AREA_FOLDER = {
    "A/5": "1oo4R0GK0nRW8CB0Op-cJID6cEADGKURj",
    "N/0": "1i4u2jWbRdR-pHdW2oimH2J9H_NfoqA6f",
    "G/4": "1OqsZeypP8hOInDVeFb2s1nixVx1KFuLr",
    "H/3": "1hxBHw1DGpJqa8a-hvXqjg5AcZY44NifN",
    "I/1": "1AXVvUu6knQbiD2yHG3DfcE89XGficRyX",
    "L/1": "1Kn_Qd4zAvUqWjFsucHz8Id2fyyQzlMSO",
    "K/0": "1KU_h53s4fYZRT9WRKplwVhWw4_6ofymd",
    "C/4": "1EDTiJ6QvTDOnWw9shCyjgd8QU4uDwrpA",
    "D/0": "1crrzcjQsx-QRbyiKn1ulXCgHfjdkqLpC",
}

# What each area is, in the order a disk fills them (SPEC.md §74.6): the
# collection's coordinates, the USER AREA OF DRIVE A it lands in on the disk,
# what it is, the line to type at `A>` and its titles.
#
# EVERYTHING LANDS UNDER DRIVE A, and that is a decision about the FLOPPY and
# not about CP/M: a drive folder is a folder in the ROOT, every drive letter
# sorts before `RUNCPM.O88`, and five of them push the package off the eight
# rows the Disk window shows - so double-clicking the emulator would mean
# scrolling to it first. Drive A already exists (the master disk is `A\0`),
# user areas cost nothing, and `USER 6` is a shorter thing to tell a session
# than `N:` `USER 0`. The collection's own coordinates stay the key of
# everything fetched, so `--from` a local copy still works and a file here is
# still the file there.
AREAS = [
    ("A/5", "A/5", "Yahoo Software's arcade games (1982-83)", "USER 5",
     [("LADDER", "climb the ladders, dodge the barrels"),
      ("CATCHUM", "eat the dots, dodge the ghosts"),
      ("PM", "PM, by the same hand"),
      ("LADCONF/CATCONF", "the terminal - choose 10, H19 (ANSI)")]),
    ("N/0", "A/6", "dungeon crawlers, with their levels and monsters", "USER 6",
     [("NEMESIS", "the dungeon of Nemesis"),
      ("DM", "Dungeon Master - build the dungeon"),
      ("CASTLE", "Castle"),
      ("PERSON", "roll a character before NEMESIS"),
      ("TERMDEF", "the terminal, if the shipped TERM.DEF will not do")]),
    ("G/4", "A/7", "GAINA, a video game", "USER 7",
     [("GAINA", "the game"),
      ("GAINAP/GAINAPA", "its companions")]),
    ("H/3", "A/8", "Borland's Turbo Pascal v3.01A", "USER 8",
     [("TURBO", "the compiler and its editor"),
      ("TINST", "pick a terminal - ANSI, or H19/Zenith")]),
    ("I/1", "A/A", "ZDE v1.6, a full-screen editor, and PMARC", "USER 10",
     [("ZDE", "the editor - ZDEINST picks the terminal"),
      ("PMARC/PMEXT", "the archiver, and its extractor")]),
    ("L/1", "A/B", "LU and NULU, the CP/M library archivers", "USER 11",
     [("LU/NULU", "make and unpack a .LBR")]),
    ("K/0", "A/C", "Super Directory and friends", "USER 12",
     [("SD", "a directory that sorts and totals")]),
    ("C/4", "A/D", "R.T. Russell's BBC BASIC v3.00", "USER 13",
     [("BBCBASIC", "the BASIC, with its own editor and assembler")]),
    ("D/0", "A/9", "MicroPro WordStar v3.30", "USER 9",
     [("WS", "the word processor"),
      ("WINSTALL", "pick a terminal before the first run")]),
]

# The files each area carries: name -> (Google Drive file id, SHA-256, size).
# Regenerate with --refresh, which re-reads the collection and prints this
# table; a difference is a hard failure at fetch time and never a warning.
PINNED = {
    "A/5": {
        "CAT.COM": ("1Kgvh5gQXGD8ckB63fgGcB1k0UxPt639U",
            "cb98a90cf965827f5754ac80d8220a5c0032eb013cfca1fc0c5187489a27ca11", 3840),
        "CATCHUM.COM": ("1OTsaN2q4b8xj_HatAGjo-uFVhMiZjqyB",
            "290be6961c0ce655ed63f36005a020f30da38a0726add10dc04fee4c4e95b1dc", 28160),
        "CATCHUM.DAT": ("1WvyMkKZQ1VcCILNvSJaBTSggCtWrsXbV",
            "7ab7a257059cd949e2a032542ada6efb1b126b4ca6d8b004a53891c3cc7b2852", 512),
        "CATCONF.COM": ("1-XouUK52OZCbl7Nrc7_u0urq8duMKdAA",
            "5a9549ce701ad3b9f74902ef5429c66f1a5063e9aa94444b22fc85785d5f7aaf", 22528),
        # HITCH.COM: not carried - Infocom's, and its data is above the size limit
        # HITCH.DAT: not carried - 113,330 bytes: above the 65,535-byte limit
        "INFO.TXT": ("197bRGpMzE5QPf47WBDIDikro14_cUv5P",
            "f8afd08c40764817f4b77d07071ad685d6f2dbb6e58b346d0fd2f16ade0cec39", 20),
        "LADCONF.COM": ("1NWSpdV7rcyrwgf2r21J1U36TL1LNSi0H",
            "7198470b1d0bd59b5129d60144dc3faf0ad7af96009242a8762d36c33f1d14ec", 22528),
        "LADDER.COM": ("18BiIlEan1fCOFqS5lerVhRFlxTnuf5yu",
            "583399fa98acbe725d15a91f5a3028b195fbe0b28b09d93e26199a90d72d0e67", 39936),
        "LADDER.DAT": ("1P2A_smgoWXb8C4nkeGR0Y75WsAlev621",
            "f4fd191d98f114f4201d8eb25b193343d591383b7ccd1ee0eb8b3f2c4713209b", 512),
        "PM.COM": ("1f0IvOtdJMWL3-b9YbHit9pUY9PD25R6U",
            "29cb8f806161d9a6894954f65159990aa0b60bc705eabbf70fd2063b9c67c3c2", 36864),
        "TERM.DEF": ("1f51vL514sm_-wlHQceROtACXt51dEBA0",
            "a24a513762cdb119c0eae07672b714a88c59e6bbb5def0efda2f6d0228465368", 128),
        "TERMDEF.COM": ("1ei0bkZggjG6OIm_uPFhJVRbjkCUAaqDx",
            "fe0ce1bde141457b7869b02b524690723d238fe804e1d69dbfb0fe2774b4b6b8", 45440),
        "TERMLIB.DEF": ("1rzajBMf8Nh0XjuATI4l893jKr6DUe1GD",
            "088cd0b1fff85113e25ff39217abf5f0ab5641f0ff4808a8216aecb056a5a30f", 5888),
        # ZORK.COM: not carried - Infocom's, and ZORK1.DAT is 76,288 bytes
        # ZORK.DOC: not carried - Infocom's
        # ZORK1.COM: not carried - Infocom's, and its data is above the size limit
        # ZORK1.DAT: not carried - 76,288 bytes: above the 65,535-byte limit
    },
    "N/0": {
        "ARAGORN.CHR": ("1TJ8idzwTZ1LIdXd2y9tI-dZgfLJY8Fn_",
            "8c02db2488a7ec327d40863f0b2cb2522486a6fe3a4ea756eedc1a505d3b8a0a", 640),
        "CASTLE.0": ("1wioBqDt-rlVTjfPlRP3EufhTHdikJUUC",
            "67b02d98b564d0b175c5c571108367be8eef778e6af322f2406f5648677ce2f7", 7168),
        "CASTLE.COM": ("1Nz1ktIRjKyKxiFp4Lda6YTbHurRxQrRX",
            "2dfac8aee79426b6dfb1632a830870130730bb5e22f5c5824d323ca8fdac2e34", 18816),
        "DM.COM": ("1dfXpEsQWYo9ECeCy_vinLGXdqPWw6Qhz",
            "4649f1843a3a7e2dcfcbc588a11c04e64d14e2d183842b033255ce572064546e", 21888),
        # DM.PDF: not carried - a 5MB manual
        "DUNGEON.COM": ("1WUdvj8VQ89boI1nWlD7_jPm157cBrPEP",
            "6ebecd85f2e7958e25c8f99650edf7cbe5ac83738662983481962d5650b727b7", 32512),
        "INFO.TXT": ("1ai1Xx3e21IsuI4yIXTKuGw8C-BA0jeFn",
            "3f8b284dc5229b8bb9ae5b00df5f6c39e0b42c9f66370c2b824027743727806b", 26),
        "ITEMS.1-2": ("1CvteNRF2cH9VJ0zt4MijBqi2_oMSUTPV",
            "510de593f6fed7ca70446ed733c42bb6fea5ea1729b2d516c9840fb030a6bf0e", 640),
        "ITEMS.3-4": ("1FcoFa4pBgnGBoAS4gbDCBARaOjYX6Oit",
            "10784aa83abc26d93d2731696fb06c10f9db345f351a66553740756dd40c6742", 896),
        "ITEMS.4-5": ("1mibiCkgE1azhpd4tHGC3HySz8zimsP8I",
            "af957e804f8b9fb3e755b8df60ed76595d7c3548715335a8515c362ee61e1a0a", 1792),
        "ITEMS.6-7": ("1VdsBDcgfpbWEnoJ1xrHyuXE3DSZas8OL",
            "726886c7ce1196b09fb61af6141e0a22197dd81874ca630a24961fddc19d7336", 3584),
        "ITEMS.C-0": ("1EG2X3Y9LkJRCVYrFIW2tGgBiNfhLZJn8",
            "b8b2333d151eb65874a2e84c11ec786d387e8c87869ab7059f6e6ee75ce73ac8", 768),
        "L0": ("1yRsF_sj8AO-LM-UQLpXTrwoNenJrL_W0",
            "da35677b66dba4d9a5269cd184aeec0600ab71e2a3534bdbf1662aa1aefb8656", 1792),
        "L1": ("1iWWFFxcyY7b8hBJ022hILksw16mlswP9",
            "0ae877f94da44570f092edd0579506135c8c0a2912376cc35679b50b14a0020b", 1792),
        "L2": ("1iJi_86vomGAi3TZJCZNoYhT44djftI84",
            "253e64ee21ec998ea4b020bc98e43600a81bf1dd3c2171cfd6ad48e9cfefd8ac", 1792),
        "L3": ("1U4tF7sUf2D3KU060Zm1I8nhq2dgoYFfZ",
            "1aa4a7be0b4e2f6dc5387825c69e5f05be1ee0ec243ee0bbaf28ae5244e57c7c", 1792),
        "L4": ("1asN8W8Y8hjcTUykYE9s_QYxlkV2C2CWa",
            "e7432cca765e18cf0c79b00d55fe0413cb888d44f962bc53c60f245f967c4665", 1792),
        "L5": ("1vNp6rT48PLZZoi4FDfDoLO_1Z_q0d5_3",
            "c4ea609f93fa1bbd0694c2a7bf64617f0a5a7239ba3aa54802464f82116d3c05", 1792),
        "L6": ("1vKSszXXc5KWVjvHgCaE25f_OPb30w_02",
            "83e6bc91a87cd7f796afe524a461ead1ce89cb589aeddc2b715524bc7778b687", 1792),
        "L7": ("1rptYAc_MbIoLHlsnsmd9QWDcPNDmRPiN",
            "8f6ab7f4c852711cef817953321ec1c2f53b5ab99d3b071e64338e1bbafb254f", 1792),
        "LEVEL.0": ("17FpZGJcSLGaRvV-dRP7F67pj9IOS9SQ8",
            "5a14f1597a40129e0552de1f1f196fa93dac532299756668ced155ab38b7a0ba", 7168),
        "LEVEL.1": ("1faWYF9ZykpHQUt-ozO4-FKXiRbbBAWdT",
            "7272bfaa0d48d422869015072545bca95fdf1d2990ede4bf7c87faba23b362e5", 7168),
        "LEVEL.10": ("1sZndHd0VcevgaGx6B2PYhKObAu-7miDS",
            "c4ee282531434ebf1c71152b48f5dd7bbb821445b12ebb16f85d7550e0a6e9be", 7168),
        "LEVEL.2": ("1RGhIF8TsyHIgAwzr3ezOmboOQIAmYnta",
            "15295fa6c79a5b08900eab5b8c9685fa42fcab4966fa3f560b41e396b101bace", 7168),
        "LEVEL.3": ("1dPNJMyHsRnkUPspjdytlXyKRHZuYypgv",
            "c311c86f11bbce54d51353314a4f659adf6e25f4861247f97101406f23d35203", 7168),
        "LEVEL.4": ("1pMQusZIz8hr-8ziE5TTMz3g3k4_h7Zx7",
            "edabcc7de0cb9ea3f7ca6e3f0aea0139cac153695f526a4522a5d7b25af32b9c", 7168),
        "LEVEL.5": ("1V5JtebLsqzbWjpepwnh6yAg5eL9i0Tsq",
            "177e1a9b830bd7688676c11c703057aa2cee0e0aef1c11e05804b500a2962b89", 7168),
        "LEVEL.6": ("1waiSwFJxYlOZgPilML_6dmzmxMT86UY-",
            "d12aaab2c80f90156619b067f42d0e8fe135380d7444aeb0a1eda850fd46c896", 7168),
        "LEVEL.7": ("1MyfgTGHNazueqzcF2KzdrLIey61bcu--",
            "0cf135e2b56af7d77fce455037f418c3b4f8ec874e0f9ba4a6556e87564b68c8", 7168),
        "LEVEL.8": ("1XiLiv-CWpvp92BzFk7jxNDN3W3TD8aOh",
            "82ea956251ec3021315cded3de8599a91e9c9466b6a86565bbfff6ab16280979", 7168),
        "MONSTERS.0-1": ("1_cnwvNCspoNJP6WyUC79F0DfPhaRISdF",
            "4a9ea9f39b6477da88421812b81e959cbb593168f8b6c639f29d5d5c3c8b9a71", 512),
        "MONSTERS.2-3": ("156w635YyzPcbFGOz-4MlYKj26SNvAB_w",
            "a6e9475584bda295c78d1325c636e4c39f79a6684c6bc95c680214d3ae959eb9", 768),
        "MONSTERS.4-5": ("1bttnxijq-QwSL6MpX8nmL8stca_u5CBF",
            "bb9728ca193b7cec52a86236759e203edcdd25194b9715b8f8a11b588e8f65a3", 896),
        "MONSTERS.6-7": ("1nI6xwEn0ki2lM3D0iVFru5FMRG8tt8T9",
            "ddbb8a9ed1a5a95108cf2b1fee155523caa6bbca2a31170f5e66de8a6a2f29fa", 768),
        "NEMESIS.COM": ("1SBPDc3_HPWd_QXV95vy6wb6tfVSj6uxI",
            "1128937c3fa28bf136217750126ae2a81ad45795a6e9e25e5e7a4b6fd2e6a732", 9216),
        # NEMESIS.PDF: not carried - a 12MB manual
        "PERSON.COM": ("1HtbEJ6xFd0VFvIuRsgrZ9aZEwhR4svIE",
            "8f8dc9e27f389c9a4385fa3dc87ec3614ad1d9aa3dbe0902907481e909e0d5fa", 14336),
        "README.TXT": ("1MOfaMm6s67QIgW6tEyQK2CnxRu8zNtfe",
            "2234fd4f78b6646c53da9301069c1fa01866573cca1362943dc811740f4fe822", 483),
        "TERM.DEF": ("1XQX-f_StZ_dv8jtyCDR1KUj6VohKPO1e",
            "524e0ebec234ff21b10de3c3806f087ecc437cd62a4984c64e7dc20b58eab131", 128),
        "TERMDEF.COM": ("14AR-WwH_5JG3zosrfwFATREDJj4dIXzf",
            "44b70f958eb7c74cf729a05a5d6b9ca9762f5eb2198eac5c3b071afe4e366d2b", 25856),
    },
    "G/4": {
        "BARNA.ABA": ("19epVOEAWR0FGQPSWr15w0PAywyve7Gv0",
            "adef2938471f2f920480fb82a889c9b0761b9897a2e15947915d8481bcaba111", 2048),
        "BARNB.ABA": ("1A5AvamHweZoolmrKlkqpjIDLZSy8rE1m",
            "6bc8792cc42ee71f056be74c616ef1c9fc04a3c15ea6052751c7918b1412393c", 2048),
        "BARNC.ABA": ("1s11YlZTkmwI-xSxcJ9gFkzXnInDWMWXm",
            "745f76ee0b5120e994fa30a583b94fc28c1a63715b57f16999c88480fb465251", 2048),
        "BARND.ABA": ("1ECGeSJb4q8S08eiahluYuqq2n_1OerKk",
            "ef698c14fb90c4d237bafc3565fae5ca62a2b3a2e574e2a78277bd3c9d833d6f", 2048),
        "BARNE.ABA": ("1ib59ibalUNvGR7HKYGf7ICHhM17pYFfK",
            "393340d656539fd25b06c88eaafab7a4a935b605ce1985f98d1c61f21e2987b6", 2048),
        "BARNF.ABA": ("1yL5V4E5LWM5Ffu486IDqPI4CdVtBlEbb",
            "c3a28045857f2d2d53168600e19fe4bc7c95996ceaf8fe3ce56f1cc59041f7c6", 2048),
        "BARNG.ABA": ("1VsbHFrlLUS9F89ajCkQCeX1vnoXKXpMW",
            "8dddfef8f6739da7bce71e3cd06ff78bfb146867a0e9870d0c9662640eea5f30", 2048),
        "BARNH.ABA": ("1r3IvAvivgz1fX_bmv09az5JU6SNYYqNS",
            "7688b6467f868675b42103e0550fa48181ef0d96eff7b45a3074e7613ce1f53f", 2048),
        "BARNI.ABA": ("1ob7jADRrngk7W9bplIBtGRJyavddjyXL",
            "c9357ed6b146b3ce46fd2079f9a8bf6ec20a06ea0b67c00f14f6e489d1e3c49e", 2048),
        "BARNJ.ABA": ("19U3WvpXfNWhC-CwgvoabWZBQziLWh6gt",
            "daf16164177f9e89a3f9e1c37fdc6bdfa8f7de269ff757e46da3d8f99c4003a3", 2048),
        "BARNK.ABA": ("1acRNgD_JM0jLzIxA3u2-Cka0OB6C22s1",
            "e006281a4c2341c7f4071ba06ebc656a1fd2357ae6de77b0cd3db8220b52a2c5", 2048),
        "BARNL.ABA": ("1gtFfRRCNEaQ3bq9t9OVZsLNnubRawSxu",
            "337ddba2e8b6cf0350e9e5ac18a40ca2157989fde3ee1f4d4231599c92d5c7f4", 2048),
        "BARNM.ABA": ("1_mcS4x5vabP3Ydpd1nH06JWaRiRQWkxZ",
            "38be649566a696b918d9d7760e5fd53e4d45b24121f15f33f1b3cbcd3bc8d84e", 2048),
        "BARNN.ABA": ("1Nix-Dz-SEY6tu3XkzxW5PZucS1xneM_k",
            "765bfba3765baa84a740523cad93b395ef58e914fc681c63d63ededd82d5b020", 2048),
        "BARNO.ABA": ("18T_AbgSY50SMB1dEirpWEMdnL9NZAq9c",
            "3d76ba01cfaac129ee018708251c1da077be44aa0bfff7eb6834632570202d7f", 2048),
        "BARNP.ABA": ("1DeBjo25sZ2qKgz6gzPc_gwQrJSYkQoai",
            "c1e1c7cc96234cbbd92294c131e404e74c37050ccd0f032cdefa12b558c05098", 2048),
        "BARNQ.ABA": ("1ugK5bXNb6OZp1_iFuewshmZC29oTzcC1",
            "32c6b75dfe9263105085299f69c9168dcad5c248a206b9931f9249306b4f51a6", 2048),
        "BARNR.ABA": ("1bm52H0_pc63RYC20EcaTyx4Y5HLIMeeb",
            "09a8b82d97a6d1a3f5c1647a1ac53059beaee490ed4205bf3e3e8609c2db0432", 2048),
        "BARNS.ABA": ("1hTtLjM_tw5kV9DU2JJkp1vWDsWMtBEHB",
            "6add247ab69e36546cc80eb379648290f30ab2fa443894fc8cd061bcb3607daf", 2048),
        "BARNT.ABA": ("1ZG6bPViHEstcbOvxCJG4ZOzY1U22QaSt",
            "abc434886bb57f5787345015aa2842475f842366910c7256d86423385041c69d", 2048),
        "BARNU.ABA": ("1S2qSU5bjOzfFME7XtC_lSu9Im2KHbwuB",
            "f95a25f4d34e3d0284b717671b56747de89f4a6a663eb844baf9c9dd00d99a29", 2048),
        "CITESTE.TXT": ("1hvnSvJJihqJsPwVkp3AhuGQ_Wx5WsJRO",
            "49d94d1a6b4ec6a327cc8c4fad7c1134ef16e9c9899666e7a53022b175f963a4", 77),
        "GAINA.BGN": ("1l80vYi4jrz6lvDdECaYuDMLMRiy8qBAt",
            "a2d6b1b2b12c7cd54dad2ce57fd6a286eeee08ba108c190d3817ba5a163d408d", 1024),
        "GAINA.COM": ("1YSsj0gpEpL5QhaUPGljpMQBdOgvqcra9",
            "897a86689929072b3de034fc73c4a87a4a27cc7807520f87ee2a59293a7232f3", 5632),
        "GAINA.HLP": ("1mw2h5sAiE4f4NOFqUQu06RTHdsNFsGHq",
            "fd2ef89fb21badc17c247840534aa0e19d6e0c00bad55fc41a34284bab6fca57", 10368),
        "GAINA.TOP": ("1bVx3tlAIxfnEt8K5bEZd7-4FuRKiiuPj",
            "3c132b9293ee7e53702651ce7470b652d93d1a03c2f491739e44166df2161fce", 1024),
        "GAINAP.COM": ("1F1JqTRsNmmbztuGBxlX848yxEnktUT7N",
            "51c613232c5b06200a359b55a0e1dae9867275b9df224e1206451ae6ffba72ec", 5632),
        "GAINAPA.COM": ("1RlRTC852Yyg50XArDscJOAxTqOgqEMlN",
            "0498d59662fd5a9ffaf5b04352adba3cb14c1910b84d2c290b992109f8210ad1", 5632),
        "INFO.TXT": ("1DncfmR-Cke5e3HALdDZ1qgY5XNrVbmDD",
            "0e8bfbadddd3d220cdde484681e66c20b7df0711151f43d826b4c8e28b296ad8", 16),
    },
    "H/3": {
        "CHNGDIR.PAS": ("1b5GTKgpCxxOxKmK2esQCFIjTCFcC-1i5",
            "c705face8c18a78ad68546d99c9409bcb7f7fa972818e5234885e03fcc2b2eb5", 1280),
        "CMDLIN.PAS": ("17nQabdNWzSp1LyPDzgAhtC9GLdrhMSm0",
            "5a6ac5b9cbed937e81fa9e14e161221ff63b0f6ade0a39655cee550ade601fa3", 768),
        "CMDLINE.PAS": ("1GWrrxDMhQ2PepjBPgPfSce3ZzW1snMqF",
            "76b4097006808c82e6117ac55c4e2cef0825a10401eea9e3d80441988694810e", 768),
        "COMLIB.PAS": ("1_LB8bIQKtlblp8N0HvEp-I9_qvXxvuSs",
            "c59999a68a91860b63f692128fc356d104e57e2847e976bd62ce48ba01b4ff12", 6528),
        "CPMDIR.PAS": ("1wvF3fwfUy0doI4StHOEiAgQ8VHWFVaVz",
            "6d2fe1e59ef88d7b21a62adae1ce37b667c35e8a4cf5ad5d02bd683278731866", 1152),
        "CPMSTAT.PAS": ("13ScHBQgtqprD_eLjNVaGBRG1w-j5vx3E",
            "3750883e58a095d155e77d2f468f4eef5b2ad90242302f0a2b7be3fa94518fc2", 3200),
        "DEFAULT.LTP": ("13ZS7Oqdl0qp8j8-fpOCsccD96vKbOHE3",
            "e0d7c4c6460b53e7c95d649a02b5eff79f3131d080e1dfe2ebd142ffbae7b3b7", 1024),
        "DIRECTRY.PAS": ("1tO_FnKyU9v9WpyDOGpv-p8x8PcY8mU5R",
            "d4f99b7d1dc6cd580d507e4a3c8c9476c92af510e7776ad1dfb95ce553c3406c", 2432),
        "DISKSTUS.PAS": ("1NHWenAAIhXC5tgRsM4ZsqjaWBmWPgOrF",
            "50d3bafffd32a16a40139e0be6231d01b58c1c37068bac643d231e98fbe66014", 3200),
        "EPSON100.LTP": ("1zWDoedeOVg3YDCp7bSRWepFh1edCZQAy",
            "c3c6675d1393a94e1a922f50f22622f1e4413534ba76d57bf8ede7f99f6d4bf9", 1024),
        "EPSON80.LTP": ("1A1zYIKK3djy72UmiZUNos3MDtyBn0zO1",
            "c1706668aa663895cdc10d46f74a2dbd37507541f7192bfa51e387bd1fa23476", 1024),
        "FILLCHAR.PAS": ("1396DtG13UT92yjSDcbR3H2gHqvUj-oSo",
            "2d800bcbf9e451c6bab0691e3538729a3f0627b19bc6a39d9c6f664c8164e8cc", 1024),
        "FILTER.PAS": ("1XOpNkc_mGoIrQY4lytBsQZK3Msgap-c4",
            "4b6f3145cc651db1689b6a967ab1eded280c9c968d381d9d86e3d6c26eaeba06", 1408),
        "FUNCKEYS.PAS": ("14bC_Fv5yIROjLgn-4WakYxN0H5oq_UlX",
            "c8967242952eaa15376d0e7293fca709b6783467dbcdbf0f35e6b44b144123ec", 896),
        "GAME1.PAS": ("1GK2NYYwp9EvO8k3GGpml-Ot4vSX5HUpK",
            "311d0923b47d15782d76319c9f2e3f585233acae8d4f2bf8846ae9b6f020c39d", 5248),
        "HELLO.COM": ("14Aauq5ffvlw2L1zLRt0BnXiH5np1_7av",
            "d414654688fde99e181bf7882e6f1908304511a7ff4eac35522eba8d6e10aaec", 8320),
        "HELLO.PAS": ("1sp5sSxWJQCIDZPy-EjbNp0nBtgpF0rOA",
            "e0dec6546b48283900b38136bfa7c46b486c3d1e74eb13401fbbe2993899ca27", 128),
        "IBMINT10.PAS": ("1v_RYg4fIB8aZnXWd4sqUlBWd1lijgcmu",
            "266e5e5ac2530e565fe56da938a0ea12e9c6d0e8aa456fea7f91bfeb85cbbcb3", 2048),
        "INFO.TXT": ("1v0kccBPYoJk979ZpXycb_i6KjhncRJcW",
            "af20230c0bd1cebb8b8c5a2893ef59393e90eed41f7f3139550dade8bc4bec67", 19),
        "INLINE.PAS": ("1jV6x_OXeD8fOC-EsM-ffPoQRaCYqp5up",
            "afd549f157311b35fbdbd2dc000cbacd51476d5546a720bc46d2141c9ebe133d", 896),
        "IOERROR.PAS": ("15mwoBb0URbJz2ypSE6Y-XoHX05KpSqIV",
            "4d73a44f3468d88b4a9b0a2302a9c7bd59f588eca5834b51cd990ed1a88d7ea1", 2944),
        "LISTER.PAS": ("1JuBZwiaMVZLi4QVR3FRSMVkI4mg_kvua",
            "176f9c8556d2a1f0e9b99f3d61a166e37841e0a9244afb9a0a783638882dccbf", 5632),
        "LISTT.DOC": ("1K5llZhFyl13-N_RNRK9yYi0-AjcBlh0m",
            "e30e43a2016b3063aff68140d14a65fed80ce420ed5e49e6b2787a65a69b1ef9", 5248),
        "LISTT.PAS": ("1Ce5QoGnssTqqJTo7xZ8T1gGHLVj0Nh_e",
            "85ce6f94f998a4463be850bda7b2581a39dfbff38b69f4528dd8c5829883072e", 16512),
        "LISTT2.INC": ("1txolE34tOJo8Xsi3NXnydmEZa5JuMT9S",
            "8f23cd285620c1eafe1aef9b313d15b07cd7e14b2e4759de1529714a15260a0f", 20224),
        # MANUAL.PDF: not carried - 6578073 bytes, above the 65535-byte limit
        "MC-MOD00.INC": ("14cPlMRW7XnLAopG5FuK9_HWRrFO1t4tk",
            "9eedd3464b8a1721b05dca15a438c0c478c2bc8b728632015d35780eed6eb662", 1024),
        "MC-MOD01.INC": ("1WA8lphT0YQuh7uqM8oJwbsFPPp8hwfJ4",
            "620eabe19f795e6d8ec42008675d2247c862e89dc7ad0b821fdb543ed61c32e9", 1792),
        "MC-MOD02.INC": ("1P7xihPfwo8-DpoJRzekdj8TPUpefxSYs",
            "777cd965e1c5858e4db4e5f72f906b6a61876206b5c1f15a5e5bc23577962e22", 3328),
        "MC-MOD03.INC": ("1E7qT4GK2oSdN8R2r0KXg2fjMs4fg6Bln",
            "62c24a59f9cb8903f98fe7471912c1ef7aae9a4f958f565c1275026b07ddb29d", 4608),
        "MC-MOD04.INC": ("1wMQpHN1hsWvy1jvsIWEYXwXl6UcR3Zg2",
            "d261c5831c1a5448a822d859dc96ee713b7a535a4fc4b88e3c61baec66a3049f", 8960),
        "MC-MOD05.INC": ("1lhSHE9OlZCbzlzdlE8MmB8BO7rewQFv_",
            "163aaac3bfecdea30159eae72ee8f5d882829edf55dadc9fa35730bef7181985", 11264),
        "MC.HLP": ("1bw13rI3Hv4gsyk5onUR1vVWemFQFtSQQ",
            "4b7e8ccbb0dc74152a7eba3a40d8761e139b750f4d466fd3feba0b53c77221d5", 4736),
        "MC.PAS": ("1jjAkF87lF8xfuFlHQNlFTeicKrTWYoPf",
            "2778376ea77168cfeeff5ac80ff07573eb96bf42e7283f91c1a1ffdbe2c5bfa3", 6528),
        "MCDEMO.MCS": ("1v2tGcwllnI7G5Avy-uvJomVjpvnsjxzz",
            "a5b4df43f9636f6be58b1acfa12f90e24262b5e3e108a0489a961824f5f4359a", 11904),
        "MEMSCREN.PAS": ("1ZeM2za-8HG_nIvn0WW0cbFfmPDkFOSAS",
            "30b78bf5e255a6758e3517317a97834a7338106e933e2234173a0f5bb2572335", 2304),
        "MYNAME.PAS": ("1V9gm_nXHBaXizPu5yLf-SJhGHtkpWGjD",
            "660c057adef76f439e47c8853a739084b527dc1ab6cd217cf14e90abd3c9da3e", 384),
        "OKI82.LTP": ("1PnIuhm5wRhpu33l1hcHQGO-0694TtXVN",
            "767407756853a85dfa28699ba9bde65514cc9f4d5b1f302ff9ccf09d12a9c0b8", 1024),
        "OKI92.LTP": ("1u38Dn8GF4SEd3YeLJwlWQQJm9-tcFvAz",
            "4ef0a968998e4f76f1f9c3afeb484a0591371ff1c0f49770b07197c44c81def4", 1024),
        "OKI93.LTP": ("1Twf4ciNYbNswMNA2lm2c3HwCZZUSFm1q",
            "02468e114dc460d1720711f7a71e46148ca1aada828ffd74a2883666d3686d3f", 1024),
        "PASS.ASM": ("15Q7QZt164sVgLB_oniFkvcVfX7JprRrL",
            "570a5bd2642a2065888d564eb297fec870618cffbc473c9161bb6a71528835c0", 1152),
        "PASSFUNC.PAS": ("1QXApBsgYdtl0G0k5256RguNoeU4Xb7Ze",
            "727cbd70632d50e558aed529f0587d95758e8a8644fc0a1dbf6f152f896694b7", 512),
        "QDL.PAS": ("1nY9EnlyiLQiSl5ePCexTY-9gECaUSbvQ",
            "958af194bf3eb1c75c6ae0b9ba0664ac2be8b29cdf949482d82ac5b83652846c", 9984),
        "RANDOM.PAS": ("18iJGiw8GKyrEODK2vpdQG2EqRHa2lWu_",
            "0d4fe63a022a98b04fc90295dd941595721b7636766dec6aa6208dd5ad73bf57", 3712),
        "READ.ME": ("1H2yBb8Z5I60_CPeY0DF6osdgSmkuwSLb",
            "e8bc2721c3a6246822b919a0b01647fe79b8e608cc74e2f7a0788fbf80a8d042", 6016),
        "READ1.DOC": ("1zf30odHuQMDZTHO5Ww5Eszt-PoXN20BA",
            "6d6f138fa19d7c0ee8e0db8672a5a2e3c642e2e32ef16c05e66d79239adce957", 6784),
        "SCALARS.PAS": ("1Lrk9B4vEZEtoQCgdNZAgYXpTeB0HUiGi",
            "bf7d330f606705aba4f86a661498c68b4f54f57c1173f6ccbb227cbf78a2a9c1", 3968),
        "TBOMOUSE.PAS": ("1FjX44IlHw_MOg0Sweqnc8G5V7xC_GsxT",
            "36b4331aa28b71ca5965dd779f56371f697ccef268de6b56029e27d779f442b0", 1536),
        "TINST.COM": ("1JDREhe_7C_PZSvCHu0KLDq32rtYACu4W",
            "210fc32edda0f1ef5372273e1a35aef845dc3fb37746cd351238e6bd1fbc939f", 25472),
        "TINST.DTA": ("1H8ZzWXU1AdfOyBHWIUvZHh0OYPr2r7Z6",
            "3847864faa080fccbd02b4cb22de5269ca30577d9be1442d9b23d7a48075025f", 4736),
        "TINST.MSG": ("1a33zxruhLEp2zlOFeB3QwKCZicrJqNLv",
            "ecf67ff3e3adb5160c5c19c2cca792bf0f0cbf1a2eaf4670520a96447e71f1cd", 3968),
        "TURBO.COM": ("1SLmF2NACBqur2OHw17vs87fGLb-b0Aiq",
            "453bba10ddd4ed75d4ae0bfffae94d4268c96a84f39fef8e63d99f159abc8d6b", 30720),
        "TURBO.MSG": ("1Iv73Z4DdflHvDLZ-F7BwG4pSpWIP3NRh",
            "bb6d210a71f147b0c26516d357ee7ca8fb2732b5393a7f856655d9afc63f44ea", 1536),
        "TURBO.OVR": ("1jXuQk8sup4ItUrhyEybB7JrLsxWLLGiS",
            "5cba0924c92b5945a3cc73b574cde67dc30e36595cf510d11867675abe5fff41", 1152),
        "TYPEAHED.PAS": ("1N5UG9ZQBfnku5jxYaSl-phqM1y4hkkfb",
            "8b9f265a31172b1a05fa330ac104adeb707ee21fb7632e93ebbad25dec647dfd", 768),
        "VERSION.PAS": ("1IHj8Z610wMMDV2vioXuFDEb58vS4Fm4L",
            "8e7404dc8409d9e285a61daffb19ffd476af317ee35609f04b081e5ddd18087e", 512),
    },
    "I/1": {
        "ADDENDUM.DOC": ("1RnCdrx7RPIAhv6Iq9OnQG4KvGFDFRAd5",
            "c47dff6798528ce5e37c5b5919e399d8a4f262f48799c11b6fe694d08ba12897", 5888),
        "INFO.TXT": ("12BOg7KjVhC66O78GMyIKozZbKrwzors_",
            "f9798f1d8e3e3a669441ed3581e3d1aa68204a40a9a10023b2fce9265277c850", 24),
        "PMARC.COM": ("1t3YtLS0QYQoJGND5GzKHJ4CH00Fnnd1w",
            "28aa664d70d6c464d459a7e2db8be5b2a22d62d4d41485e5acd58f27b7ce32da", 10368),
        "PMARCEXT.DOC": ("1trwSkM3eFysecn3kHmHeDK_g5SMJZQbV",
            "8713903abc4de55597b5af7a2bd7115ce657700ec923d5742e632efadfe305c6", 768),
        "PMAUTOAE.COM": ("11Q391F995sZDXTwdOd1-Bj0csby5yw-Z",
            "595c6977e4730a72cac2c2fa5ebb7867683207e347d145caad24d06f2df7ab50", 30976),
        "PMCAT.COM": ("1pQB_JTNd5NvvkDiPu2bWVjElEvG3uA0i",
            "508cb6e794018c3c27f44d3d4ec0e868dfaebd30e513973ac0d8fe80f27385bd", 6400),
        "PMCHGMX.MAC": ("1mb57wNUOHrQOMbDjoe-MijYVzm37o7-v",
            "0960214452c969e86f2def3e2b7e77193c1ae0166292777e2c3aa804600d2791", 6656),
        "PMEXE2.COM": ("1d9w_rCK-FoBGlbl6IsCAmcKSsAZMjR0G",
            "15cc342d4d4b6eb96de7590682c379d409f2e280a7c6a7ccc716be68ffaee799", 1536),
        "PMEXT.COM": ("1M-ta0OOhGCIdZnCS1ZRTJwMiaa_DrLar",
            "3c5626f2629a82c3d214b6c1a88d28f4f411bd1801ac6b7cc8640cebbd9990f7", 12928),
        "PMSET.COM": ("1lCoYqzAzHvwU0KYgKee-DmRRIrHrtYT1",
            "b3370f7a4fd3e6d94a3d7bd0063f64918f2ec75d977cf5e660d13dfc2cd3fe1a", 3200),
        "PMSFX2.COM": ("1lI-ncco1c20lM4O1wdf1QQXlHhC14fzm",
            "268c2839053f1bea09812c99aacb0ec3ca0cf491aaf6ff613ad6e2f62059873a", 2176),
        "READ.ME": ("1Rp9bzP0NxF48ILJuHJur0hIHvpgJs6Xk",
            "d7b782d4ce0e25c37f5200a18abf365282645dfd57b71278b7728022a6fd66a7", 1536),
        "SAMPKEY.DOC": ("1e_aqm5KDx3yTl0Ny5kQ3AQC8bDZYhDx-",
            "171a742a7fa9c170d9c78c29284b45d7030dd99fd07b9c2c8275a7d8e76a4d5a", 3968),
        "SAMPKEY.ZDK": ("14gN0gE8WoSaLFGg6hK5M00Q2i5c2017r",
            "24955b40a019b77d441a80139cf690db525d690ccd5a03c92e0f90f87d942db6", 512),
        "SAMPKEY.ZDT": ("1yURDlcgnXIaVS95Lpn9FxZNCXhnDuKYd",
            "62321b4cc46818eca4b5ff4deab0f0b1866a7d04096824200a8c2fe292ef0eec", 384),
        "ZDE10.DOC": ("1VP4E2M4-19LNjoU4wKttAOXDjxqbhYfi",
            "6009460c3bd1328255cadb2c7dec8569c36696dac5298d1d2639cf5d68fec352", 48640),
        "ZDE16.COM": ("1VUlXikycf28pC444_b1Hd2VPtlDnKGmJ",
            "a89dfb2de5b5ec10f2b50a42bc3a4fc05070045254aac0aad0d3f605708585c5", 16896),
        "ZDE16.DIR": ("1E2qChLwzkPv18uBbj2TeHshrK0Tf79h3",
            "4c4d59222ec6895ca2408a9708c2ee9d404e55a979bcd9b91de7076e8b1b60b0", 640),
        "ZDE16.FOR": ("1hBa_MJ4VACDRtdhehNI9MG4QbTW-Q2vI",
            "d0417c9d49968fc4a4e186630b674f9fe93cc68c42731521a2f39cc82452558b", 512),
        "ZDE16.NEW": ("1GLTOrScHQ5B4nK6N8mbHmDVXiDUXutxd",
            "7a520b1829d780258cb1da712e9b41c14e9842a089d642c91007b326b425923c", 21760),
        "ZDE16.PMA": ("1pNA-gHDn_KwW09vFOWahqjVVcApD_DrE",
            "b8d37d7b3912ff99195df35cf8f6ae61abb1da72ce49da7466a916616ed01aba", 58637),
        "ZDENST16.COM": ("1raN9-OE81Ja8c7qij7sixRlkJgaRmBb8",
            "e34f7a7f027fa486c1ad2160571e722f5f5f3629a5feef5f6f872472e9e54e78", 10624),
        "ZDEPROP.DOC": ("1Z7oPfT3nCGqwzrPQ1n-STJpa8BAstBSc",
            "d508b7b2b3c407daa9b9a001fe2cd5ca052ef65d6bac1285d1c497f824113148", 7168),
        "ZDEPROP.Z80": ("1qu4YEjwjJVBEMSW1F_bcWMSkfl4ALhY8",
            "778846e4cb40f7bd715b0515ba1e0333878618f35d42bc7e678b0309672045a0", 1664),
        "ZDKCOM13.COM": ("1oT0ME_aj7J_EsiSOKGKg04QQc1vTgO8J",
            "e94ec62f8dc08edcb6dd2711eb78ca09b256c0e1c01f1ed70e32e02fd9387bf3", 1792),
        "ZDKCOM13.DOC": ("1BBVW4YBrCmYEJU8PEOy0l3VpIHgRYIaF",
            "7a1baddbf3441fb6e516a121431aa99c51698d0f409bcbfb6cd7b5eb37afb5fe", 9984),
    },
    "L/1": {
        # APPLIB11.LBR: not carried - 7223424 bytes, above the 65535-byte limit
        "INFO.TXT": ("17YApmNgZ1xW631hv28kpnTYZ45MZ7-_b",
            "92bb776ec829d6fb84cf79841672491728395b7511ceac9b67222f59c03d0091", 11),
        "LU310.COM": ("1JZ66sYVaGlVujyZLYAkbBILtAv0AhRfU",
            "ba39c7d1cc826c41398690f5825625c185909edc12232fdad75b0e2adcd1ba4e", 20224),
        "NULU.COM": ("122P5HuRaDc8HSNF22AvOszNDgDxvDWn2",
            "126dd1a427b1fc14bcb5f1ba21e232728a9b0c68fa21cf304ad474db995b4fdd", 15616),
        "NULU152.COM": ("1-mTTH8sJy9qP6o3Og9ecZHzEV65fD7V2",
            "5a04dac8fd69fc8d4661527bc605ccc9ff01a575caa193ff50fb7e2445718428", 15616),
        "NULU152A.LBR": ("1zQqpUyMgYGulImjNY_bGFvsAKdRVCJsy",
            "fe639a87b883a96019ef2b23860aee48685454035967608bc7c4465e3a1de8e2", 56192),
        "UNCR28.COM": ("13w3h85nox4rh-PrXoj2wAmAKuUBeAEnX",
            "5ccea696fe3d4fc1970f766680387298882048d51385b48baff90176a410d771", 8576),
    },
    "K/0": {
        # -020589: not carried - empty
        "INFO.TXT": ("1CLOBufd4hNJEGPfs4hlPFWoPxDP0GeMb",
            "642b0a90b848887229a25067bff9dcc91528e2a6d65af5dee868a8743eacd13b", 15),
        "SD-RCPM.INF": ("1QY40e4vnDWtoAR7DqUBkWNJEMMz-1F33",
            "5757064d5b23f703a5162b345403d93ed9ac02d1e8a1beb7be2202be29b8e877", 5120),
        "SD.COM": ("1ZotWvNDJY8TYsz5QhKFdZXS3rtV2mGIT",
            "c72e855b62965b40485e5f1d9ad6d22268351982dd2b836e65ad0560a70b3969", 5888),
        "SD.DOC": ("1jhEDiX3jvtaSIXxHeZuLxllrLSUl8Us5",
            "5ef8b2451fa5a0845198e682b2bc8e8799fc71eb3caa9dd45fc23b3918a865b1", 10880),
        # SD137.ASM: not carried - 120422 bytes, above the 65535-byte limit
        "SD137.FOR": ("1JqDOLp5QtAmiICP4CtG1UNuQK11uUW8V",
            "85577780edf23038e5f8a64b9ab079b6654266a281b815b1709154fc06efeea6", 384),
        "SD137.HEX": ("1_PlKmkN17lyOhdVr9bQYNk3cF14Y12WF",
            "f9e524fcf00d68620db54e7ddf4f88ed73c7943c0f1993bfb0ad466ca0200215", 16256),
        "SD137.HIS": ("113EUS9sV7jvuulT-0q73BFwkDj36EDCN",
            "1e4490e8753b682753d1360e66a1ae248b59e58bcf4d8070879bdaa48e877a6a", 20480),
        # SD137.LBR: not carried - 95616 bytes, above the 65535-byte limit
        # SD137.PRN: not carried - 207872 bytes, above the 65535-byte limit
    },
    "C/4": {
        "ANIMAL.BBC": ("1uk4dnxaWzOGQjBV1VHSk1v2d1yvzT7vG",
            "0c2cbeecf79987a9efd5b87cde7394b96b16d4dffbce8686d4c15231acb8066f", 3072),
        "ANIMAL.DAT": ("1M0v-OhyYGs5Ien-oO0I8wt7ga4hUMkQV",
            "1c1cc85481315833be157a94806dba8b5cea56fe055dfd071ed37646af261410", 1280),
        "BBCBASIC.COM": ("1-Rnu57h7pDEaOWwGSH6-_hZmqeAUg8fU",
            "74fb5fecdc970deb57ddd96d728f85306acaef11dd171e9c1d99ffe669100901", 14848),
        "BBCBASIC.TXT": ("1ywd5phF-sBe8l-5PrrJsSpaOB5ro9Gsq",
            "b1b176439e942202b6d18a2e266ddd7347a273d118ee233d447f6bda29e59970", 15360),
        "BBCDIST.MAC": ("1ttwe4Qc4gljJd8jEC_y7LO5bt7ZXhmvS",
            "89f311b446b24775cfb6adc7bf192fe0fa8ec54c92ddd0f2592653946e9d2965", 4352),
        "CONVERT.COM": ("1pRotPNkSC3JVILS3qox24zZXbFCY3zMA",
            "3c70d9f792679bbed2bf1ed062fec9cd6799f090fe95cb4391515708df589224", 2304),
        "CRC.COM": ("1xZyf9_O9apNZguWhGIQzqTcKNmr3A-P9",
            "86ff38a2c57930717ba5f4735314a3fca44baa54f9a60c31bb70993501692474", 2688),
        "CRCKLIST.CRC": ("1OodcMqRITZ8yVxP8iBXVlSOT6vc7koZW",
            "6a4ef722e806810f8cc61c0ff4c3493865cd2c7c39414de1e92ee634f0e7a117", 896),
        "F-INDEX.BBC": ("1MlFTHj24gnMikHP7ABrWJJeQfTmfBR_A",
            "1dad23a78f6de54b60e49c2bc4a58384e05ec17f9f8f5942c38bfd3c64e6879e", 6784),
        "F-RAND0.BBC": ("1PNGnmKJtOCAwuKKMF1g2YUZraFTP9Co5",
            "a6f140b204f71445e3a8f384007c80633a181ed6af1ce5ef0aa88aa17f16fc25", 1408),
        "F-RAND1.BBC": ("14Iflbn9GZWKApckbDLwiADVXDLdHJozg",
            "4fa2351b0b9dc8dbc641f52d2fc277eda8e90c74a9eb83c288331aea3dd70f3c", 2304),
        "F-RAND2.BBC": ("1RuXxT4jX7ecTHWv0zkLQ6C5ShFqW9qgB",
            "752b4ac3dbbae40eeff2e36ea7a08160fe0ae756620dfaeb30a74d1ff0a4957d", 8064),
        "F-RSER1.BBC": ("1FehqXpUN9e_HNV9ivx7_5tIQ7GpU4Fzh",
            "ee8f22792c18f7f5a31a343d6c40094e880c6d42637a573b4f7c8a630963b38d", 384),
        "F-RSER2.BBC": ("1Js5YuqYdaSTuR-b114pfk42kDOdF25_s",
            "9db0134c2cd4d9707fcc31a911345d779e0c63fec3dadf922e98c2abf52444d2", 1152),
        "F-RSTD.BBC": ("1oT_2CdNd5K45sVJObXrmsi9akN55SoAU",
            "664c9cc8a15d20c061ff72d941bcf9f8a620c573a8e318d3a1470ae211e9e427", 1152),
        "F-WESER1.BBC": ("1N9t3dIcYis7wGOkeermfrL3xTQTTSt_I",
            "075bfd6d7769e678c80dc25cc7c5905eb834a4e2a118627edbae0ad176f9b745", 1024),
        "F-WESER2.BBC": ("1ekJVUJINUOK0m9aAHkXQf4DvU1qvkKAI",
            "1e53be375968f6c6d2390cdbbc25f3e125667213d1e88606e9dd8e0774c29995", 3072),
        "F-WSER1.BBC": ("1P0XOwhILTENJ5GW_fx8k5G1AlCzy2AVb",
            "f4d1fd8d8b9c7fcf19cdd1b37ec8c0bfa30dea85fa654be0a17462d2cf25da2d", 640),
        "F-WSER2.BBC": ("1Zt_R_qepDMQPOn4ohTkjEMvTzcgi-UHp",
            "c8ea1a96a1e678a36cd906585077e69af0cd690bed646d714bc1eedf5d83a892", 2944),
        "F-WSTD.BBC": ("1wm5e4Mf7zsssoTkyszT542Td9KSEzxpN",
            "c7196f0b3f33878b9167a68be2b42b8fa8fb812000bdb9243c0e02ed1218a0c6", 2560),
        "INFO.TXT": ("1deAZ0pCCt9HcAH2JMs6NaZUwLvxrNL8T",
            "4fd2a62835ac3c82f3eb9ba7215234969b113fbd3eddb455d3e8c005d518ee0b", 15),
        "MERGE.BBC": ("1UOOpX4VXZi0pxUNXhQVApfS-CI50nal6",
            "843df636f04954e0de1db39a046ddd5042357ed59024833b50f81e2bf6c8bbac", 768),
        "READ.ME": ("1m1z-DneqqZaFDytWHE3olS91w9IE3pq4",
            "f7044e19fbb6d975aef65eec8c404813f39f0350a6cc0a58b17844d2b96fb8b9", 1408),
        "SORT.BBC": ("1kt4aRPRCy9cI2SObnEUjezqgi7n3T_hL",
            "a4370a92a4b903e9563bddd639a2959800d096d64521cf7fe94505e631486a5b", 1920),
        "SORTREAL.BBC": ("1FIHcS7K-uJoIQqqcgVnzGuQYblUFlO9Z",
            "e0331e856c9ffb226c835a8a7abf13f345c2a85c9fd91ed954a8836ad827fc85", 2048),
    },
    "D/0": {
        "CD.COM": ("16k9D9VWRXx5-kd9D3A6KkiJv6h3Zd3Ml",
            "b726106285279b0044e58b8dccaa0e0c57fd2077b359711a2c6f9f809d73c683", 5632),
        "INFO.TXT": ("1kCaSL5QWJHcCciEnLik5SV-9S64TJXzz",
            "80aac2f99859464376bb59c90abe86ba709ef9ceaa69c32d693dfa2bb954ebfe", 23),
        # INSTALL.PDF: not carried - 7232625 bytes, above the 65535-byte limit
        "MAILMRGE.OVR": ("1QvXWYN7iTZRcFwDz2ulMmPkeA2s9Zxql",
            "bcdd6a0c26e3dba0b443016d5c7cae16f67ccd5537493734da83a1bd68773747", 13312),
        # MANUAL.PDF: not carried - 7321205 bytes, above the 65535-byte limit
        "PRINT.TST": ("1L-aR41C74kQDw5EcUu2UK4R9j1ecVZDK",
            "41bde58133eec627aa724ab922cf4c200375b39686da67f195aa39100b1482a3", 4096),
        "SAMPLE.TXT": ("1J0u-NoO1umHtQXwN8fSstlUQkIrqFNQP",
            "2a4290ab8ff6957e8844e23fb0d87c8abfc7376bf80a6bed19a43cbb6fc320d2", 3328),
        # SPELSTAR.DCT: not carried - 97280 bytes, above the 65535-byte limit
        "SPELSTAR.OVR": ("1ULcU_p0-ioMC642i9PntyldPwAE6Htzv",
            "bddcacfacc33462624b7cb56bb9ed9741b66b1d470cb501b8ea001b88483b48f", 17920),
        "TEST.TXT": ("14QL5aoBYbq0fFlJAH5obnwiNUKOOTG4x",
            "eab0348a16c98669136db2ce0ab1a21c4b269e25157481cdb4db68b95cde38f1", 128),
        "WINSTALL.COM": ("1oDxYBsqhMcYJOcwHAbc7ZM_Btgz-NUu3",
            "aeb19f846074627e7f4c525dc0806915f48c2befd6596160f938fb1a626cb996", 29184),
        "WS.COM": ("1YX-bkBh2zhl9UCy59qB2nAq61ioixP7g",
            "87a8972658b7184fc95eb43e62a8bd53b87177ea94023e1d57dd2cfc950b0cda", 17664),
        "WS.INS": ("1JVJcrXMfAYzPNz1DQndjgxmvm3qyP-Jl",
            "95f7c1e04730f4a52bfb1344b4b1851b31ac818c67e58b6c4d30644dabd0ba1a", 58240),
        "WSMSGS.OVR": ("1Eab14GAbx-yC2QYgdHs0VN8grmDkbJfG",
            "65c3231fe18dcfcddf2f5580553bb6ab2dc4af6c95e2e9bd6424813c0abc57df", 30080),
        "WSOVLY1.OVR": ("1VbAzTOIiV4_MHhRb-3XamgGfsofcXrg7",
            "71f212196dd8e1aca9f9e16b85823da2f0fd391070e2e3f12ffcf333d8382074", 33536),
        "WSU.COM": ("11X_GEOQp6lU4hcy-rEUqYZs1OjqkYYEQ",
            "71a6dfd8f099c44f0791671473540811ce6c9959c2140606757470c54b48b7e5", 17664),
    },
}
# What --refresh leaves out of an area, and why (a name it must not put in
# PINNED). Documentation orders of magnitude past a floppy, and the games
# whose data files are above MAX_FILE - which --refresh would refuse anyway,
# but naming them here says the omission was a decision. --refresh also drops
# what it cannot ship whatever the name: a file above MAX_FILE, and an EMPTY
# one (the collection has one, `K/0/-020589`; os88disk.py refuses a zero-byte
# file, which is a FAT12 entry with no first cluster).
SKIP = {
    "A/5": {"ZORK.COM": "Infocom's, and ZORK1.DAT is 76,288 bytes",
            "ZORK.DOC": "Infocom's",
            "ZORK1.COM": "Infocom's, and its data is above the size limit",
            "ZORK1.DAT": "76,288 bytes: above the 65,535-byte limit",
            "HITCH.COM": "Infocom's, and its data is above the size limit",
            "HITCH.DAT": "113,330 bytes: above the 65,535-byte limit"},
    "N/0": {"DM.PDF": "a 5MB manual",
            "NEMESIS.PDF": "a 12MB manual"},
    "G/4": {},
    "H/3": {}, "I/1": {}, "L/1": {}, "K/0": {}, "C/4": {}, "D/0": {},
}


def fail(msg):
    print(f"getcpmsw: error: {msg}", file=sys.stderr)
    sys.exit(1)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


_opener = None


def _open(url):
    """One GET, carrying cookies: Google Drive's download of an executable
    goes through a 'virus scan warning' page that sets a cookie and posts a
    form back, and without the jar the second request gets the page again."""
    global _opener
    if _opener is None:
        _opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    req = urllib.request.Request(url, headers={"User-Agent": "os8088-getcpmsw/1"})
    try:
        with _opener.open(req, timeout=TIMEOUT) as fh:
            return fh.read()
    except (urllib.error.URLError, OSError) as exc:
        fail(f"cannot fetch {url}: {exc}\n"
             f"  The games need the network once; after that the output\n"
             f"  directory is a cache and nothing is downloaded again.")


def drive_get(fid):
    """One file out of Google Drive by id, through the scan-warning form."""
    data = _open(f"https://drive.google.com/uc?export=download&id={fid}")
    if data[:15] == b"<!DOCTYPE html>":
        page = data.decode("utf-8", "replace")
        m = re.search(r'action="([^"]+)"', page)
        if not m:
            fail(f"Drive answered {fid} with a page this script cannot read.\n"
                 f"  The collection may have been made private, or Drive changed\n"
                 f"  the download form. --from DIR takes the files off a local copy.")
        action = html.unescape(m.group(1))
        form = {html.unescape(n): html.unescape(v) for n, v in
                re.findall(r'<input type="hidden" name="([^"]+)" value="([^"]*)"', page)}
        data = _open(action + ("&" if "?" in action else "?") + urllib.parse.urlencode(form))
    return data


def drive_list(fid):
    """(id, name) of everything in a Drive folder, off the embedded view -
    the one listing that is plain HTML and needs no API key."""
    page = _open(f"https://drive.google.com/embeddedfolderview?id={fid}#list").decode(
        "utf-8", "replace")
    ids = re.findall(r'id="entry-([-\w]+)"', page)
    names = re.findall(r'flip-entry-title">([^<]*)<', page)
    if len(ids) != len(names):
        fail(f"cannot read the folder listing for {fid}")
    return list(zip(ids, [html.unescape(n) for n in names]))


def write_if_changed(path, data):
    if os.path.exists(path):
        with open(path, "rb") as fh:
            if fh.read() == data:
                return False
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(data)
    return True


def get_file(area, name, out, check, src):
    """The bytes of one pinned file, from the output directory itself (it is
    the cache), a local copy of the collection (--from) or Drive, verified
    whichever way it arrived."""
    fid, want_sha, want_size = PINNED[area][name]
    dest = os.path.join(out, *area.split("/"), name)
    data = None
    if os.path.exists(dest):
        with open(dest, "rb") as fh:
            data = fh.read()
        if sha256(data) != want_sha:
            data = None
    if data is None and src:
        p = os.path.join(src, *area.split("/"), name)
        if not os.path.exists(p):
            fail(f"--from {src}: no {area}/{name}")
        with open(p, "rb") as fh:
            data = fh.read()
    if data is None:
        if check:
            fail(f"{area}/{name} is not cached in {out} (--check does not fetch)")
        print(f"getcpmsw: fetching {area}/{name}")
        data = drive_get(fid)
    if len(data) != want_size or sha256(data) != want_sha:
        fail(f"{area}/{name}: SHA-256/size mismatch against the pin\n"
             f"  expected {want_sha} ({want_size} bytes)\n"
             f"  got      {sha256(data)} ({len(data)} bytes)\n"
             f"  The collection is not the pinned one. Check it by hand, and\n"
             f"  `--refresh` prints a new PINNED table once you have.")
    if len(data) > MAX_FILE:
        fail(f"{area}/{name} is {len(data)} bytes: above the {MAX_FILE}-byte\n"
             f"  limit SPEC.md §74.3's whole-file record model has. It cannot be\n"
             f"  opened on the disk, so it is not put on one.")
    write_if_changed(dest, data)
    return len(data)


def fetch_all(out, check=False, src=None):
    """Every pinned file into <out>/<DRIVE>/<USER>/. Answers the sizes."""
    got = {}
    for area, _, _, _, _ in AREAS:
        got[area] = {n: get_file(area, n, out, check, src) for n in sorted(PINNED[area])}
    total = sum(sum(a.values()) for a in got.values())
    nfiles = sum(len(a) for a in got.values())
    print(f"getcpmsw: {len(AREAS)} areas, {nfiles} files ({total} bytes) in {out}")
    return got


def clusters(size, cbytes):
    return max(1, (size + cbytes - 1) // cbytes)


def area_cost(sizes, cbytes):
    """What one area costs a geometry, in its own clusters: every file and its
    own directory, priced for the spare save slots it ships with. No drive
    folder: every area lands under A, which the master disk already pays
    for."""
    return (sum(clusters(s, cbytes) for s in sizes.values())
            + clusters((2 + len(sizes) + SPARE_SLOTS) * DIR_ENTRY, cbytes))


def select(out, geometry):
    """The areas a geometry carries (POLICY), and what they cost in its own
    clusters. Answers (chosen, dropped, used, sizes)."""
    cbytes = {360: 1024, 720: 1024, 1200: 512, 1440: 512}.get(geometry)
    if cbytes is None:
        fail("--select wants one of "
             + ", ".join(str(g) for g in sorted(POLICY))
             + f", not {geometry}")
    sizes = {}
    for area, _, _, _, _ in AREAS:
        d = os.path.join(out, *area.split("/"))
        if not os.path.isdir(d):
            fail(f"no {d}: run tools/getcpmsw.py -o {out} first (or make cpmsw)")
        sizes[area] = {n: os.path.getsize(os.path.join(d, n))
                       for n in sorted(os.listdir(d))}
    chosen = [a for a, _, _, _, _ in AREAS if a in POLICY[geometry]]
    dropped = [a for a, _, _, _, _ in AREAS if a not in POLICY[geometry]]
    used = sum(area_cost(sizes[a], cbytes) for a in chosen)
    # GAMES.TXT rides A/0 beside the master disk, not in a game area: it is
    # what a session reads BEFORE it knows a game area exists, and it is
    # written whether or not this geometry carries a game - a disk that has
    # none says where they are. One cluster, and one directory slot that
    # getruncpm.py's own arithmetic prices because the Makefile passes it in
    # A/0's file list.
    used += 1
    return chosen, dropped, used, sizes


def games_text(chosen, dropped, sizes, geometry):
    """The disk's own note: what is on it, how to reach it from `A>`, and
    what this geometry left off. A CP/M user TYPEs this; it has no SPEC.md."""
    t = ("Games on this disk, beside RunCPM's master disk.\r\n"
         "\r\n")
    if not chosen:
        t += (f"None: the {geometry}KB disk has no room for them.\r\n"
              "The 1.44MB disk carries them all.\r\n")
    for area, disk, what, how, titles in AREAS:
        if area not in chosen:
            continue
        t += f"{disk}  {what}\r\n"
        t += f"  from A>, type: {how}\r\n"
        for name, blurb in titles:
            t += f"    {name:<16} {blurb}\r\n"
        t += "\r\n"
    if chosen:
        t += ("TELL A GAME WHAT TERMINAL IT IS ON before it will draw.\r\n"
              "Run its setup program once - LADCONF, CATCONF, TINST,\r\n"
              "WINSTALL - and choose\r\n"
              "\r\n"
              "   10) Heathkit/Zenith H19 (ANSI)\r\n"
              "\r\n"
              "and NOT the DEC VT-100 entry: this terminal is ANSI, and\r\n"
              "LADDER's and CATCHUM's VT-100 setting adds 32 to every row\r\n"
              "and column it sends, which wraps the score panel on a real\r\n"
              "VT-100 too.\r\n"
              "\r\n")
    def listing(areas, heading):
        out = heading
        for area in areas:
            what = next(w for a, _, w, _, _ in AREAS if a == area)
            kb = (sum(sizes[area].values()) + 1023) // 1024
            out += f"  {what} ({kb}KB)\r\n"
        return out + "\r\n"

    # what this geometry leaves off, under the two headings that are TRUE
    # from where the reader is sitting: an area the big disk has (go and get
    # that disk) and one no disk here has (it is in the collection, and
    # CPMSW= puts it on a disk of your own)
    bigger = [a for a in dropped if a in POLICY[1440] and geometry != 1440]
    never = [a for a in dropped if a not in bigger]
    if bigger:
        t += listing(bigger, f"The {geometry}KB disk has no room for these, "
                             "which are on the\r\n1.44MB disk:\r\n")
    if never:
        t += listing(never, "More is in the RunCPM software collection this "
                            "disk was\r\nbuilt from, and on no disk here "
                            "(CPMSW= in the Makefile\r\nputs your own "
                            "beside these):\r\n")
    t += ("Zork, The Hitchhiker's Guide to the Galaxy and Colossal Cave\r\n"
          "are NOT here: their data files are 76KB, 113KB and 68KB, and\r\n"
          "this port reads a file whole through a 16-bit count - nothing\r\n"
          "above 65535 bytes can be opened.\r\n")
    return t


def refresh(out):
    """Re-read the collection and print a PINNED table to paste back in."""
    print("PINNED = {")
    for area, _, _, _, _ in AREAS:
        print(f"    {area!r}: {{")
        for fid, name in sorted(drive_list(AREA_FOLDER[area]), key=lambda r: r[1]):
            why = SKIP.get(area, {}).get(name)
            if why:
                print(f"        # {name}: not carried - {why}")
                continue
            data = drive_get(fid)
            if not data:
                print(f"        # {name}: not carried - empty")
                continue
            if len(data) > MAX_FILE:
                print(f"        # {name}: not carried - {len(data)} bytes, "
                      f"above the {MAX_FILE}-byte limit")
                continue
            print(f'        "{name}": ("{fid}",\n'
                  f'            "{sha256(data)}", {len(data)}),')
        print("    },")
    print("}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-o", "--output", metavar="DIR", default="build/cpmsw",
                    help="where the files land (a cache; default build/cpmsw)")
    ap.add_argument("--check", action="store_true",
                    help="verify what is cached; never download")
    ap.add_argument("--list", action="store_true", help="print what is shipped and exit")
    ap.add_argument("--select", type=int, metavar="KB",
                    help="print the files a 360/720/1200/1440 disk carries, "
                         "'<DRIVE>/<USER>:<path>' a line")
    ap.add_argument("--cost", type=int, metavar="KB",
                    help="print what --select would spend, in that geometry's clusters")
    ap.add_argument("--slots", type=int, metavar="KB",
                    help="print the os88disk.py --dir-slots flags --select's areas "
                         "need, so a game that saves has a slot to save into")
    ap.add_argument("--from", dest="src", metavar="DIR",
                    help="take the files from a local copy of the collection")
    ap.add_argument("--refresh", action="store_true",
                    help="re-read the collection and print a new PINNED table")
    args = ap.parse_args()

    if args.refresh:
        refresh(args.output)
        return
    if not PINNED:
        fail("PINNED is empty - run --refresh and paste its table in")
    if args.list:
        for area, disk, what, how, titles in AREAS:
            total = sum(s for _, _, s in PINNED[area].values())
            print(f"{area} -> {disk}  {what}  ({len(PINNED[area])} files, "
                  f"{total} bytes; from A>: {how})")
            for name, blurb in titles:
                print(f"    {name:<16} {blurb}")
        return
    if args.slots:
        chosen, _, _, sizes = select(args.output, args.slots)
        disk = {a: d for a, d, _, _, _ in AREAS}
        print(" ".join(f"--dir-slots {disk[a]}={2 + len(sizes[a]) + SPARE_SLOTS}"
                       for a in chosen))
        return
    if args.select or args.cost:
        geometry = args.select or args.cost
        chosen, dropped, used, sizes = select(args.output, geometry)
        if args.cost:
            print(used)
            return
        note = os.path.join(args.output, "games", str(geometry), "GAMES.TXT")
        write_if_changed(note, games_text(chosen, dropped, sizes, geometry).encode())
        print(f"A/0:{note}")
        disk = {a: d for a, d, _, _, _ in AREAS}
        for area in chosen:
            for name in sizes[area]:
                print(f"{disk[area]}:"
                      f"{os.path.join(args.output, *area.split('/'), name)}")
        print(f"getcpmsw: {geometry}KB: {' '.join(chosen) if chosen else 'no games'}"
              f"{' (left off ' + ' '.join(dropped) + ')' if dropped else ''}, "
              f"{used} clusters", file=sys.stderr)
        return
    fetch_all(args.output, args.check, args.src)


if __name__ == "__main__":
    main()
