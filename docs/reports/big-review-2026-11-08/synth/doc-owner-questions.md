# Doc-pass questions left for the owner

The coherence readers of the doc pass raised 84 concerns about text next to what they
checked; 46 were fixed, 31 left (correct, history, or line length only) and these 7
were not a text fix: each is a code defect already in synth/bugs.md, or a rule to decide.

1. The comment is accurate. Whether to fix the hole now or track it in FIELD-NOTES or a plan is the owner's call: two Disk windows that share a corner while one hides the other can take the dragged pos during a damage repaint.

2. The comment records a real defect in code and test. On 360KB and 720KB the canary at file sector 21 lands in a run that crosses no head, so it does nothing there, and t_canary still passes. Fixing it means moving KSIG_OFF or changing the test, which is the owner's call.

3. The tree contradicts itself in dozens of places: '.bss arrives zeroed' (SPEC section 10, SPEC line 18120, kernel.asm, ui.inc:1661) against '-f bin zeroes no .bss' (section 13.3 step 3, ui.inc's ui_pcmd/ui_tm_drv/menu_dropd, SPEC 12671, 54571). Under ROM_COLD the .bss gap is shipped as padding and read by stage 2. Whether code may rely on that on every load path (ROM, HMA and hard-disk loaders) is a design rule to settle before sweeping either side.

4. Real error: SPL_RESIDENT is defined only in splash.inc. But it is inside an %error string, not a comment after ';', so a text-only pass cannot touch it. It needs a code-string edit.

5. Confirmed as a live code defect. VID_EXT is on by default on kern_big and kern_mega (kernel.asm:877). fsx_capstab and cp_vidnam each gained a VID_VESA row, but app_about_vid still has three or four pointers, so the About box indexes past the table at kind 4. Fixing that is a code change. The text half is fixed: SPEC.md 39.29 called the kind VID_VBE, which exists nowhere in the tree, so all seven mentions are renamed to VID_VESA.

6. Confirmed. dskw_read_at_x divides offset by cluster bytes with one `div word [dwr_clb]`, and the offset is bounded only by the entry's size. dskw_wabody's `div bx` takes an append's offset straight from the entry's size. A hostile size field can therefore push the quotient past 16 bits, and the result is a divide fault. That is a code defect reachable from a hostile disk, and it may want a FIELD-NOTES or plan entry. The comments describe it accurately.

7. The caveat is accurate. kern_big's %1_hma zeroes DRVR_PARA, and the `cmp ax, 1` then answers CF=1 for a class that is loaded. That is a code behaviour, not a text error. Separately, I fixed the adjacent reasoning: 'a driver image is never under a kilobyte' contradicted the walk's own note that EMS.DRV is under one. A loaded row cannot answer 0 because the image is never empty and the walk adds 63 before the shift, which rounds it up to a KB.
