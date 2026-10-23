#!/usr/bin/env python3
"""Fetch the Game Boy test ROMs RealDMG is gated on, at pinned SHA-256s.

    python3 tools/getgbtest.py            # fetch into build/gbtest/, verify
    python3 tools/getgbtest.py --check    # verify what is there, fetch nothing
    python3 tools/getgbtest.py --path NAME

Blargg's CPU, timing and HALT-bug tests, from retrio/gb-test-roms at a
pinned commit; Mooneye's acceptance tests and dmg-acid2, from a pinned
release of c-sp/game-boy-test-roms. NONE IS EVER COMMITTED: they are fetched like
tools/getapple2rom.py's ROMs, every file is checked against the hash below,
and `make clean` spares build/gbtest/ so a rebuilt tree needs no network.
docs/plans/REALDMG-PLAN.md section 7 says which gate each one is.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "build", "gbtest")
REPO = "https://github.com/retrio/gb-test-roms"
REV = "c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15"
FILES = {
    "cpu_instrs/cpu_instrs.gb": "8c5e12f41e0ba5bbca796944f92ffe6de28809198682c4332e38d1b3cf56fcf2",
    "cpu_instrs/individual/01-special.gb": "fe61349cbaee10cc384b50f356e541c90d1bc380185716706b5d8c465a03cf89",
    "cpu_instrs/individual/02-interrupts.gb": "fb90b0d2b9501910c49709abda1d8e70f757dc12020ebf8409a7779bbfd12229",
    "cpu_instrs/individual/03-op sp,hl.gb": "ca553e606d9b9c86fbd318f1b916c6f0b9df0cf1774825d4361a3fdff2e5a136",
    "cpu_instrs/individual/04-op r,imm.gb": "7686aa7a39ef3d2520ec1037371b5f94dc283fbbfd0f5051d1f64d987bdd6671",
    "cpu_instrs/individual/05-op rp.gb": "d504adfa0a4c4793436a154f14492f044d38b3c6db9efc44138f3c9ad138b775",
    "cpu_instrs/individual/06-ld r,r.gb": "17ada54b0b9c1a33cd5429fce5b765e42392189ca36da96312222ffe309e7ed1",
    "cpu_instrs/individual/07-jr,jp,call,ret,rst.gb": "ab31d3daaaa3a98bdbd9395b64f48c1bdaa889aba5b19dd5aaff4ec2a7d228a3",
    "cpu_instrs/individual/08-misc instrs.gb": "974a71fe4c67f70f5cc6e98d4dc8c096057ff8a028b7bfa9f7a4330038cf8b7e",
    "cpu_instrs/individual/09-op r,r.gb": "b28e1be5cd95f22bd1ecacdd33c6f03e607d68870e31a47b15a0229033d5ba2a",
    "cpu_instrs/individual/10-bit ops.gb": "7f5b8e488c6988b5aaba8c2a74529b7c180c55a58449d5ee89d606a07c53514a",
    "cpu_instrs/individual/11-op a,(hl).gb": "0ec0cf9fda3f00becaefa476df6fb526c434abd9d4a4beac237c2c2692dac5d3",
    "instr_timing/instr_timing.gb": "646067b3d6c79fda810e9c3f1cb7c0efd5abb0a7ac06437c54e65720c15d9925",
    "mem_timing/mem_timing.gb": "791cb418fd054b482a6ada5017e2405b96c2373a0f3914c6b23afeee76fa98ce",
    "halt_bug.gb": "ddebfb81f992132954a5580e16cfb97346a4e061c762634d7701328e23fea7c6",
}

# Mooneye's acceptance tests (MIT) and dmg-acid2 (MIT) with its reference
# picture, out of c-sp/game-boy-test-roms' prebuilt release - the suites'
# own sites are not reachable from every box, and that zip is one pinned file.
# Only the DMG's rows: a test named for the MGB, the SGB or the DMG-0 is
# about a machine RealDMG does not claim to be.
ZIP_URL = ("https://github.com/c-sp/game-boy-test-roms/releases/download/"
           "v7.0/game-boy-test-roms-v7.0.zip")
ZIP_SHA = "b9a9d7a1075aa35a3d07c07c34974048672d8520dca9e07a50178f5860c3832c"
ZIP_FILES = {
    "dmg-acid2/dmg-acid2-cgb.png":
        "cbf039fd613a6db3c3d830a6a1d887efbc892aea4578ffc569b61f66a9e07ced",
    "dmg-acid2/dmg-acid2-dmg.png":
        "ca966d50895c7efef05838590d148c2cbfd7fba57dab986f25b35b4da71abb57",
    "dmg-acid2/dmg-acid2.gb":
        "464e14b7d42e7feea0b7ede42be7071dc88913f75b9ffa444299424b63d1dff1",
    "mooneye-test-suite/acceptance/add_sp_e_timing.gb":
        "93d45e0ba554fcb66b8612e34b9c7dcd2a7a4683fef291481a0242ba25ce26d4",
    "mooneye-test-suite/acceptance/bits/mem_oam.gb":
        "0cfccd002d873a1c9cc0f9317b9e23d97b2b47d151cb413d0deb3d65e5666cae",
    "mooneye-test-suite/acceptance/bits/reg_f.gb":
        "0eeaab1343a7f85eef2a1364c849cf244dd76d4e19e25f41defdff95f46febe4",
    "mooneye-test-suite/acceptance/bits/unused_hwio-GS.gb":
        "6501bf0c271460c1abc12ab8c97acdccd5aae9bfa394f922f6de74fc8bd7fad3",
    "mooneye-test-suite/acceptance/boot_div-dmgABCmgb.gb":
        "77448f882c19da0f0a3296dbad6b147716a23428bc1a2a35f3c905dafef05fb3",
    "mooneye-test-suite/acceptance/boot_hwio-dmgABCmgb.gb":
        "01aa82657e1b4cc8dfa8b2b45ad248de240eab1330a847389453a794c30b2591",
    "mooneye-test-suite/acceptance/boot_regs-dmgABC.gb":
        "0c1800a5d4d978d11426e5e54e5bcc014b4d8d2c8a63f402d7ddf96c501f2a92",
    "mooneye-test-suite/acceptance/call_cc_timing.gb":
        "29255bd13b3104e9dbe40eaef74f5a7eb84e01acd33c3c47ccf66a88ab00f535",
    "mooneye-test-suite/acceptance/call_cc_timing2.gb":
        "6fca59e6ee031b5ab531d6b984fa4b1be5064f22aa0c9a85f8efbf705ca42b91",
    "mooneye-test-suite/acceptance/call_timing.gb":
        "85fd265448e8362adc60daf6f1c891abe5fe53f3ee095eff8ef1c08fb4d59ab8",
    "mooneye-test-suite/acceptance/call_timing2.gb":
        "5a2e8c848e461ddd3922830767e59a9f9f24628ff910e9c48e5b5e6b776aaff1",
    "mooneye-test-suite/acceptance/di_timing-GS.gb":
        "1884585d49ee8105fd952f5bbab6e0e595eefad43281073c20ecfae7eb4f7f57",
    "mooneye-test-suite/acceptance/div_timing.gb":
        "26d6c1e66750b56b894131898bc896fc9fe56aa2a46199bcc6514e658cd996bd",
    "mooneye-test-suite/acceptance/ei_sequence.gb":
        "307214288a36c73feac8cbbf242cc282eaf58df4a93e21244d4389ba9bf508ab",
    "mooneye-test-suite/acceptance/ei_timing.gb":
        "285929da5ca1268156bdfe5da2204d5fc6c2e9119fb5c2ead13b5780f73b2ee2",
    "mooneye-test-suite/acceptance/halt_ime0_ei.gb":
        "8959f1d31e0552baa4c3a8efff686ffc7a94e889e30c46e87a0c33f8f3f59172",
    "mooneye-test-suite/acceptance/halt_ime0_nointr_timing.gb":
        "34073e22bb26c0e1f4705d43394f6e0eda95bbff7706884b8ba48e0fe8e63f8b",
    "mooneye-test-suite/acceptance/halt_ime1_timing.gb":
        "3492099da389311f7a07f6de764da4ae9cef8e3fe0b7fb39878dc47d05a8f4cb",
    "mooneye-test-suite/acceptance/halt_ime1_timing2-GS.gb":
        "6093dd9a92d7cf869ccc857724706118e95931ab194f09d573504d295f7bec88",
    "mooneye-test-suite/acceptance/if_ie_registers.gb":
        "d02bc5978fb8c595486b4dcf99a6ac34e2bfc2a8756c17b1050b6cfb3720ba14",
    "mooneye-test-suite/acceptance/instr/daa.gb":
        "d01e4e7124471bfe3f4773fe20dd27167f8933ef473c43f063cc76170c9c962e",
    "mooneye-test-suite/acceptance/interrupts/ie_push.gb":
        "5f826104e61727273d59999f7796a11351a42bfc0802cad26d2a9d4cfb61d34b",
    "mooneye-test-suite/acceptance/intr_timing.gb":
        "fdcd2f8d98f26d895f8a17c9468cd5829e5bab4acb11cf053f4ed8edc1f07786",
    "mooneye-test-suite/acceptance/jp_cc_timing.gb":
        "849882b90083162964e9876d8286efe46de30849fdc8db0929d44bccc1a52fca",
    "mooneye-test-suite/acceptance/jp_timing.gb":
        "3d6e4cefe7fff78eabc14f9f8c331928bf82207c07bed830558679b6fa3388da",
    "mooneye-test-suite/acceptance/ld_hl_sp_e_timing.gb":
        "b0781b501db9cf98f7af46e37753608104fe02bfe7279ac1f7ab404e1ee25c50",
    "mooneye-test-suite/acceptance/oam_dma/basic.gb":
        "7b4c048451510fcb224a23d653edd4bb339a4cf136873e736941d29e6c60b3f9",
    "mooneye-test-suite/acceptance/oam_dma/reg_read.gb":
        "ddb2a356f4bfdad7694375e6886003c784b89ef6ef70a1731679a66791314618",
    "mooneye-test-suite/acceptance/oam_dma/sources-GS.gb":
        "66d0b099dd8902eac1e32bbf67139a50b5db70782df1616b57c1a125f09264a9",
    "mooneye-test-suite/acceptance/pop_timing.gb":
        "f5aae636e7bf15fd8f88609f3c2f030a85b42561ef82e5c16126ec75f76b551e",
    "mooneye-test-suite/acceptance/ppu/hblank_ly_scx_timing-GS.gb":
        "7ae7d817930886a2480551b0de98079e021355bac3827ee557e30afdb21d70b8",
    "mooneye-test-suite/acceptance/ppu/intr_1_2_timing-GS.gb":
        "ed481a2ead0071272056ee8f9ad08c1fc5b10d51da868d48c46a2ff900d3ed06",
    "mooneye-test-suite/acceptance/ppu/intr_2_0_timing.gb":
        "2b6e171f8e999dca4ae945c45ccbb793799bad77bc1310d170ad72a401863be1",
    "mooneye-test-suite/acceptance/ppu/intr_2_mode0_timing.gb":
        "39fd496590b444dbc85fa74bcf3f4d4e1da02c6479cb90b9c528971828c9ebad",
    "mooneye-test-suite/acceptance/ppu/intr_2_mode0_timing_sprites.gb":
        "4665062fc25e5ece8685ed4e6f9c4c589877e36ae54a10f750ed6074aa9ca9cd",
    "mooneye-test-suite/acceptance/ppu/intr_2_mode3_timing.gb":
        "1ca5c3d0c72759fa5971dfcb7d7b7c8b4864b83f615c69c97c5be2fdaf007c18",
    "mooneye-test-suite/acceptance/ppu/intr_2_oam_ok_timing.gb":
        "71aacaa7da1c84be2391ddbd143eef9ae29a3893b7c0b533c3d0eb23a8cb3e25",
    "mooneye-test-suite/acceptance/ppu/lcdon_timing-GS.gb":
        "646b9da2a500f4d7bce510f9a75100e62acf4a72a3c22344aad149cd1429a17c",
    "mooneye-test-suite/acceptance/ppu/lcdon_write_timing-GS.gb":
        "25e5dd078f730603e6f858bc4bee2005c00f39d45fb2d6de68100ad375a98448",
    "mooneye-test-suite/acceptance/ppu/stat_irq_blocking.gb":
        "bdc27c61f0efaaf171a9b85526878424f4433c1d3fec1d60ea70a0f0ce3286bc",
    "mooneye-test-suite/acceptance/ppu/stat_lyc_onoff.gb":
        "d3516dd6a1c5e50fae0cbdfb84a368a03612974b37aaf1648aa5e8b705ff7a3e",
    "mooneye-test-suite/acceptance/ppu/vblank_stat_intr-GS.gb":
        "4a0c7501fbc652dc3b2c9918d6dd1890c2bdc37f65bf721b01623772f74304cf",
    "mooneye-test-suite/acceptance/push_timing.gb":
        "956fca98e4a768da4760b732fe8f5ce55f5ddd317a2429b3ca3347f014a2424c",
    "mooneye-test-suite/acceptance/rapid_di_ei.gb":
        "25d931e2c859a5fb2fc3ec1979ac824ef5eb9f5f9f08833f03bc932b282d4c2d",
    "mooneye-test-suite/acceptance/ret_cc_timing.gb":
        "833aa0c5e5c93695291936173b7a4f18165dd178ca36262f24476751e6aff17f",
    "mooneye-test-suite/acceptance/ret_timing.gb":
        "6a481b276197f99d2189c4883ec8bdb39b6f9209e44fe982f3b7eea4a88b51f0",
    "mooneye-test-suite/acceptance/reti_intr_timing.gb":
        "1ee8d500795dfb053233aa70493ccab8cf585558eb864b0debe45621f1411de9",
    "mooneye-test-suite/acceptance/reti_timing.gb":
        "c9f2d36b5c1641c1e498599362dd4ef2d06adb9ebc07a9cd95b0a7515864a8ec",
    "mooneye-test-suite/acceptance/rst_timing.gb":
        "586b4ea3cc3fb52b84b2324b9cd5b5415f99c6d6858f6c34f7a39ba4979fe1a5",
    "mooneye-test-suite/acceptance/serial/boot_sclk_align-dmgABCmgb.gb":
        "b727e0eae7cefb9d5f3bbc9d7eaca33d69c24f745a60c6d3d2eb2a9c2f038278",
    "mooneye-test-suite/acceptance/timer/div_write.gb":
        "b647f603c71d942a5c4ab92e6d2336af3fd9729e95a845b1561bd8e7c06163b5",
    "mooneye-test-suite/acceptance/timer/rapid_toggle.gb":
        "6b3595a303447eb93220e7ab97966df2f43c7911627b84ec477d79d25e8c38ec",
    "mooneye-test-suite/acceptance/timer/tim00.gb":
        "44986744a87a85580c0113a04f014ca6c91eb5a3f26fceb9ca411bdd8044c45f",
    "mooneye-test-suite/acceptance/timer/tim00_div_trigger.gb":
        "8dfe42b6bc78b64aaafe0e1eab10e8c4c487e25285d0419b1485b2e9e0094cc4",
    "mooneye-test-suite/acceptance/timer/tim01.gb":
        "7d779278591dfa24db661fada5b99e21e0d3dcfbe2ba3b2b3a0343db367fdfc0",
    "mooneye-test-suite/acceptance/timer/tim01_div_trigger.gb":
        "d65804dcaaaeb907002cd5cdec1ecbec3edc1e7e1d95323ae306ba9a5bacb7ce",
    "mooneye-test-suite/acceptance/timer/tim10.gb":
        "99d1c44d744ea2250d1846673de16273fecc2243760c0be9653c5561fa4f1995",
    "mooneye-test-suite/acceptance/timer/tim10_div_trigger.gb":
        "f0d16cccfde31a51758d68c261c7b72529c0c1143e6ddb3423d50a0ba4c45de1",
    "mooneye-test-suite/acceptance/timer/tim11.gb":
        "cc2585b8f161f6ce2a0519fd70e85d84cf792cdacf1a7d86eb244afaa473a3d5",
    "mooneye-test-suite/acceptance/timer/tim11_div_trigger.gb":
        "14982050eb7713c149841dfd91c6d733821e1dcfb3b1c7c6fbc14e17f692821b",
    "mooneye-test-suite/acceptance/timer/tima_reload.gb":
        "1999c25db46f28570e23d4cb019bf0e6f5814a947a5a358e7dc25baa52c49f96",
    "mooneye-test-suite/acceptance/timer/tima_write_reloading.gb":
        "43d7331b6914d59b6692ad9ea2db18741c80d1b666c1386003f63b64a2488bf3",
    "mooneye-test-suite/acceptance/timer/tma_write_reloading.gb":
        "81ef3e9a375993bb3615a6c2788e8003507be1b9c58a61f629b87706c354ea6d",
}


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def check():
    every = dict(FILES)
    every.update(ZIP_FILES)
    bad = [f for f, h in every.items()
           if not os.path.exists(os.path.join(DEST, f)) or sha(os.path.join(DEST, f)) != h]
    return bad


def fetch():
    src = os.path.join(DEST, ".src")
    if not os.path.isdir(os.path.join(src, ".git")):
        os.makedirs(DEST, exist_ok=True)
        subprocess.check_call(["git", "clone", "-q", "--no-checkout", REPO, src])
    subprocess.check_call(["git", "-C", src, "checkout", "-q", REV, "--", "."])
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(DEST, f)), exist_ok=True)
        with open(os.path.join(src, f), "rb") as i, open(os.path.join(DEST, f), "wb") as o:
            o.write(i.read())
    zpath = os.path.join(DEST, ".src", os.path.basename(ZIP_URL))
    if not os.path.exists(zpath) or sha(zpath) != ZIP_SHA:
        subprocess.check_call(["curl", "-sSfL", "-o", zpath, ZIP_URL])
    if sha(zpath) != ZIP_SHA:
        sys.exit("getgbtest: %s is not the pinned zip" % zpath)
    with zipfile.ZipFile(zpath) as z:
        for f in ZIP_FILES:
            os.makedirs(os.path.dirname(os.path.join(DEST, f)), exist_ok=True)
            with open(os.path.join(DEST, f), "wb") as o:
                o.write(z.read(f))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--path", metavar="NAME", help="print a fetched file's path")
    a = ap.parse_args()
    if a.path:
        hits = [f for f in list(FILES) + list(ZIP_FILES) if a.path in f]
        if len(hits) != 1:
            sys.exit("getgbtest: %r matches %d files" % (a.path, len(hits)))
        print(os.path.join(DEST, hits[0]))
        return
    if not a.check and check():
        fetch()
    bad = check()
    if bad:
        sys.exit("getgbtest: missing or wrong: " + ", ".join(bad))
    print("getgbtest: %d files verified in %s" % (len(FILES) + len(ZIP_FILES), DEST))


if __name__ == "__main__":
    main()
