; =============================================================================
; os8088 - apps/dos/doshelp.asm
;
; HELP's text (SPEC.md 96.33.23). Two ways in, one source:
;   - assembled ON ITS OWN it is build/doshelp.bin, a lazy OP_ASSET part of
;     DOS.O88 that the re-homed host reads with op_self when HELP is typed
;     (docs/plans/DOS-PARTS-PLAN.md 1.1) - no NUL, the row's length bounds it;
;   - `%include`d by apps/dos/dosc.inc in a build with no loader to hand it
;     the row (no -DDOSKPART), where it is the inline text it always was.
; =============================================================================

; --- DHL - one help line, COUNTED (SPEC.md 96.33.23.1) ----------------------
; The count emits nothing and is what makes the sixteen-line limit a BUILD
; failure rather than a line that scrolls off a CGA in silence.
%assign DH_LINES 0
%macro DHL 1
    db %1, 13, 10
    %assign DH_LINES DH_LINES + 1
%endmacro
    DHL 'CD [path]     Change directory. CD .. goes up, CD \A\B goes down.'
    DHL 'COPY a b      Copy a file. Wildcards allowed.'
    DHL 'DEL name      Delete a file. ERASE is the same.'
    DHL 'DIR [/P][/W]  List this folder. /P pages it, /W is columns.'
    DHL 'ECHO text     Print the text.'
    DHL 'MD / RD name  Make or remove a directory. MKDIR/RMDIR the same.'
    DHL 'MOVE a b      Move or rename a file.'
    DHL 'OPEN file     Open a document with the program that claims it.'
    DHL 'REM text      A note. Does nothing.'
    DHL 'REN old new   Rename a file. RENAME is the same.'
    DHL 'TYPE file     Print a text file.'
    DHL 'VER           Say what this is.'
    DHL 'XCOPY a b     Copy a folder and all under it.'
    DHL 'A:            A drive letter on its own changes drive.'
    DHL 'NAME [arg]    Run a program, or open a document with it.'
    DHL "cmd > NUL     Hide what a command prints. No other target."
%if DH_LINES > 16
  %error "HELP is over SIXTEEN lines: CGA's band is 17 rows ([con_vrows], measured), and the prompt that follows takes one - so a seventeenth line scrolls the first off a machine nobody tests on. Cut a line, or give HELP a pager and the part 96.33.23.2 costs."
%endif
