Screenshots for PAGEDESK-SPEC 20.22.6 (the password prompt, the R6 question,
the Module cache preference). All taken on the glass in QEMU (tools/shot.py),
PageDesk built with -DPD_AUTHDEMO-style stand-in keys (^Q ask, ^T wrong,
^R the question, ^U working) because no encrypted open exists yet.
vga-auth-prompt-modes.png    VGA 640x480: asking (***_), checking 0%, wrong
                             password, the R6 question, checking 45%
vga-prefs-module-cache.png   VGA: the sixth Preferences row set to Off
cga-r6-question.png          CGA 640x200 (COMPACT layout): the question
cga-compact-prefs.png        CGA 640x200 (COMPACT layout): Preferences, eight
                             rows, fits the canvas
