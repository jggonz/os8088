# Web/native feature inventory

This checklist comes from the web interpreter's `Parser.ts`, `Interpreter.ts`,
`Builtins.ts`, and `StringBuiltins.ts`. A checked item means `coretest.c` has a
representative executable case. It does not mean the native case passes yet.

- [x] tokens, precedence, numeric literals, suffixes, coercion and comparisons
- [x] all numeric builtin families: rounding, conversion, roots, trig, exp/log
- [x] all string builtin families: slice, search, case, bases and binary packing
- [x] assignment/SWAP/INCR/DECR/CLEAR and DIM/REDIM/ERASE/OPTION/DATA
- [x] IF/FOR/WHILE/DO/SELECT/GOTO/GOSUB/ON and EXIT forms
- [x] DEF FN, SUB/CALL arguments and LOCAL/SHARED/STATIC scopes
- [x] text output, keyboard and INPUT/LINE INPUT
- [x] screen, drawing, palette, viewport, window and GET/PUT graphics
- [x] sound, timing, hardware memory/ports/registers/interrupts and binary load
- [x] KEY and error traps, runtime/environment and meta directives
- [x] sequential/random file and directory statement families
- [x] all 29 demos through far-source load and bounded execution

Every group must avoid `SB_STATE_ERROR` and leave
`sb_hosttest_ignored() == 0`. A missing host service remains a failing product
gap rather than being removed from this inventory.
