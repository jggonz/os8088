' ===========================================================================
'  MINICALC.BAS  --  A tiny text-mode spreadsheet for Turbo Basic
' ---------------------------------------------------------------------------
'  A 6-column (A-F) by 10-row (1-10) spreadsheet with a real formula
'  evaluator (shunting-yard, two stacks, no recursion).
'
'  Turbo Basic features demonstrated:
'    - OPTION BASE 1 with multi-dimensional arrays (raw$, cv, kind)
'    - DEF FN single-line user function (FNColName$)
'    - GOSUB / RETURN structured subroutines with global variables
'    - SELECT CASE / CASE / END SELECT command dispatch
'    - Block IF / ELSEIF / END IF
'    - FOR / NEXT, WHILE / WEND, DO / LOOP
'    - String handling: INSTR MID$ LEFT$ RIGHT$ LEN VAL STR$ UCASE$ ASC CHR$
'    - Text UI: CLS COLOR LOCATE PRINT PRINT USING SPACE$
'    - A two-stack expression evaluator with + - * / ^, parentheses,
'      unary minus, cell references and SUM/AVG/MIN/MAX/COUNT ranges.
'
'  Driven by a blocking LINE INPUT command line (no INKEY$ busy-wait).
'  Commands:  A1=10   B1=Sales   C3=A1+A2*2   F1=SUM(A1:A10)
'             GOTO A1   (or just A1)   DEMO   NEW   HELP   QUIT
' ===========================================================================

OPTION BASE 1
DEF FNColName$(c) = CHR$(64 + c)

DIM raw$(10, 6)
DIM cv(10, 6)
DIM kind(10, 6)
DIM vstack(64)
DIM ostack(64)

NROWS = 10
NCOLS = 6
CWID = 9
curRow = 1
curCol = 1
quitFlag = 0
msg$ = "Welcome to MiniCalc. Type HELP for commands."

GOSUB ClearAll

MainLoop:
  GOSUB Render
  LOCATE 24, 1
  COLOR 15, 0
  PRINT SPACE$(78);
  LOCATE 24, 1
  PRINT "Command: ";
  LINE INPUT cmd$
  GOSUB HandleCommand
  IF quitFlag <> 0 THEN GOTO Done
GOTO MainLoop

Done:
  CLS
  COLOR 7, 0
  PRINT "MiniCalc session ended. Goodbye."
END

' ---------------------------------------------------------------------------
ClearAll:
  FOR r = 1 TO NROWS
    FOR c = 1 TO NCOLS
      raw$(r, c) = ""
      cv(r, c) = 0
      kind(r, c) = 0
    NEXT c
  NEXT r
RETURN

' ---------------------------------------------------------------------------
' Format a number into fmtOut$ (no leading space; integers without ".0").
' ---------------------------------------------------------------------------
FmtNum:
  fval = fmtIn
  fsgn$ = ""
  IF fval < 0 THEN
    fsgn$ = "-"
    fval = -fval
  END IF
  ' Round to 2 decimals using integer hundredths (STR$ alone shows binary
  ' float noise like 1033.3299..., so we build the decimal text ourselves).
  hund = INT(fval * 100 + 0.5)
  ip = INT(hund / 100)
  fr = hund - ip * 100
  ' integer part string (no leading space)
  ipart$ = STR$(ip)
  IF LEFT$(ipart$, 1) = " " THEN ipart$ = MID$(ipart$, 2)
  IF fr = 0 THEN
    fmtOut$ = fsgn$ + ipart$
  ELSE
    ' two-digit fraction, trim a trailing zero
    d1 = INT(fr / 10)
    d2 = fr - d1 * 10
    frac$ = CHR$(48 + d1) + CHR$(48 + d2)
    IF d2 = 0 THEN frac$ = CHR$(48 + d1)
    fmtOut$ = fsgn$ + ipart$ + "." + frac$
  END IF
RETURN

' ---------------------------------------------------------------------------
' Format curRow-style integer into str2Out$ (no leading space).
' ---------------------------------------------------------------------------
Str2:
  s$ = STR$(str2In)
  IF LEFT$(s$, 1) = " " THEN s$ = MID$(s$, 2)
  str2Out$ = s$
RETURN

' ---------------------------------------------------------------------------
Render:
  CLS
  COLOR 15, 1
  LOCATE 1, 1
  PRINT SPACE$(78);
  LOCATE 1, 2
  PRINT "MINICALC - Turbo Basic Spreadsheet";
  COLOR 14, 0
  LOCATE 3, 1
  PRINT "   ";
  FOR c = 1 TO NCOLS
    hdr$ = FNColName$(c)
    pad = CWID - LEN(hdr$)
    PRINT SPACE$(pad); hdr$;
  NEXT c
  FOR r = 1 TO NROWS
    LOCATE 3 + r, 1
    COLOR 14, 0
    PRINT USING "## "; r;
    FOR c = 1 TO NCOLS
      GOSUB CellText
      IF r = curRow AND c = curCol THEN
        COLOR 0, 7
      ELSE
        COLOR 7, 0
      END IF
      PRINT cellOut$;
    NEXT c
  NEXT r
  COLOR 11, 0
  LOCATE 15, 1
  PRINT SPACE$(78);
  LOCATE 15, 1
  str2In = curRow
  GOSUB Str2
  selRef$ = FNColName$(curCol) + str2Out$
  PRINT selRef$; ": "; raw$(curRow, curCol);
  COLOR 13, 0
  LOCATE 17, 1
  PRINT SPACE$(78);
  LOCATE 17, 1
  PRINT msg$;
  COLOR 7, 0
RETURN

' ---------------------------------------------------------------------------
' Display text for cell (r,c) padded to CWID into cellOut$.
' ---------------------------------------------------------------------------
CellText:
  k = kind(r, c)
  IF k = 0 THEN
    body$ = ""
  ELSEIF k = 3 THEN
    body$ = "ERR"
  ELSEIF k = 2 THEN
    body$ = raw$(r, c)
    IF LEN(body$) > CWID - 1 THEN body$ = LEFT$(body$, CWID - 1)
  ELSE
    fmtIn = cv(r, c)
    GOSUB FmtNum
    body$ = fmtOut$
    IF LEN(body$) > CWID - 1 THEN body$ = "####"
  END IF
  IF k = 2 THEN
    cellOut$ = " " + body$ + SPACE$(CWID - 1 - LEN(body$))
  ELSE
    cellOut$ = SPACE$(CWID - LEN(body$)) + body$
  END IF
RETURN

' ---------------------------------------------------------------------------
HandleCommand:
  ln$ = cmd$
  WHILE LEN(ln$) > 0 AND LEFT$(ln$, 1) = " "
    ln$ = MID$(ln$, 2)
  WEND
  WHILE LEN(ln$) > 0 AND RIGHT$(ln$, 1) = " "
    ln$ = LEFT$(ln$, LEN(ln$) - 1)
  WEND
  IF LEN(ln$) = 0 THEN RETURN
  up$ = UCASE$(ln$)

  eqp = INSTR(ln$, "=")
  IF eqp > 0 THEN
    lhs$ = LEFT$(ln$, eqp - 1)
    rhs$ = MID$(ln$, eqp + 1)
    GOSUB TrimLhs
    refRow = 0
    refCol = 0
    parseRef$ = lhs$
    GOSUB ParseRef
    IF refRow = 0 THEN
      msg$ = "Bad cell reference: " + lhs$
    ELSE
      curRow = refRow
      curCol = refCol
      GOSUB SetCell
      GOSUB Recalc
      str2In = curRow
      GOSUB Str2
      msg$ = "Set " + FNColName$(curCol) + str2Out$
    END IF
    RETURN
  END IF

  SELECT CASE up$
    CASE "QUIT", "EXIT", "BYE"
      quitFlag = -1
    CASE "NEW"
      GOSUB ClearAll
      msg$ = "Sheet cleared."
    CASE "DEMO"
      GOSUB LoadDemo
      GOSUB Recalc
      msg$ = "Demo budget loaded."
    CASE "HELP"
      GOSUB ShowHelp
      msg$ = "Help shown. Continue editing."
    CASE ELSE
      target$ = up$
      IF LEFT$(up$, 5) = "GOTO " THEN target$ = MID$(up$, 6)
      parseRef$ = target$
      refRow = 0
      refCol = 0
      GOSUB ParseRef
      IF refRow > 0 THEN
        curRow = refRow
        curCol = refCol
        str2In = curRow
        GOSUB Str2
        msg$ = "Selected " + FNColName$(curCol) + str2Out$
      ELSE
        msg$ = "Unknown command: " + ln$
      END IF
  END SELECT
RETURN

' ---------------------------------------------------------------------------
TrimLhs:
  WHILE LEN(lhs$) > 0 AND LEFT$(lhs$, 1) = " "
    lhs$ = MID$(lhs$, 2)
  WEND
  WHILE LEN(lhs$) > 0 AND RIGHT$(lhs$, 1) = " "
    lhs$ = LEFT$(lhs$, LEN(lhs$) - 1)
  WEND
RETURN

' ---------------------------------------------------------------------------
' Parse cell ref from parseRef$ -> refRow, refCol (0,0 on failure).
' ---------------------------------------------------------------------------
ParseRef:
  p$ = parseRef$
  WHILE LEN(p$) > 0 AND LEFT$(p$, 1) = " "
    p$ = MID$(p$, 2)
  WEND
  WHILE LEN(p$) > 0 AND RIGHT$(p$, 1) = " "
    p$ = LEFT$(p$, LEN(p$) - 1)
  WEND
  refRow = 0
  refCol = 0
  IF LEN(p$) < 2 THEN RETURN
  cch$ = UCASE$(LEFT$(p$, 1))
  ca = ASC(cch$)
  IF ca < 65 OR ca > 64 + NCOLS THEN RETURN
  numpart$ = MID$(p$, 2)
  ok = -1
  FOR i = 1 TO LEN(numpart$)
    d = ASC(MID$(numpart$, i, 1))
    IF d < 48 OR d > 57 THEN ok = 0
  NEXT i
  IF ok = 0 THEN RETURN
  rn = VAL(numpart$)
  IF rn < 1 OR rn > NROWS THEN RETURN
  refCol = ca - 64
  refRow = rn
RETURN

' ---------------------------------------------------------------------------
' Store rhs$ into (curRow,curCol) and classify it.
' ---------------------------------------------------------------------------
SetCell:
  WHILE LEN(rhs$) > 0 AND LEFT$(rhs$, 1) = " "
    rhs$ = MID$(rhs$, 2)
  WEND
  WHILE LEN(rhs$) > 0 AND RIGHT$(rhs$, 1) = " "
    rhs$ = LEFT$(rhs$, LEN(rhs$) - 1)
  WEND
  raw$(curRow, curCol) = rhs$
  IF LEN(rhs$) = 0 THEN
    kind(curRow, curCol) = 0
    cv(curRow, curCol) = 0
    RETURN
  END IF
  ' Explicit formula (leading '=') always a formula.
  IF LEFT$(rhs$, 1) = "=" THEN
    kind(curRow, curCol) = 4
    cv(curRow, curCol) = 0
    RETURN
  END IF
  ' Plain number?
  GOSUB IsNumber
  IF isNum <> 0 THEN
    kind(curRow, curCol) = 1
    cv(curRow, curCol) = VAL(rhs$)
    RETURN
  END IF
  ' Looks like an expression (operators / parens / range / cell-ref)?
  GOSUB LooksFormula
  IF isFormula <> 0 THEN
    kind(curRow, curCol) = 4
    cv(curRow, curCol) = 0
  ELSE
    kind(curRow, curCol) = 2
    cv(curRow, curCol) = 0
  END IF
RETURN

' ---------------------------------------------------------------------------
' Decide whether rhs$ should be treated as a formula (vs a plain label).
' A formula contains an operator/paren, a range colon, or a cell-reference
' pattern (letter immediately followed by a digit). Sets isFormula (-1/0).
' ---------------------------------------------------------------------------
LooksFormula:
  isFormula = 0
  fu$ = UCASE$(rhs$)
  FOR i = 1 TO LEN(fu$)
    ch$ = MID$(fu$, i, 1)
    IF ch$ = "+" OR ch$ = "*" OR ch$ = "/" OR ch$ = "^" THEN isFormula = -1
    IF ch$ = "(" OR ch$ = ")" OR ch$ = ":" THEN isFormula = -1
  NEXT i
  ' cell-reference pattern: a letter (A..F) directly followed by a digit.
  FOR i = 1 TO LEN(fu$) - 1
    a1 = ASC(MID$(fu$, i, 1))
    a2 = ASC(MID$(fu$, i + 1, 1))
    IF a1 >= 65 AND a1 <= 64 + NCOLS AND a2 >= 48 AND a2 <= 57 THEN isFormula = -1
  NEXT i
RETURN

' ---------------------------------------------------------------------------
IsNumber:
  isNum = -1
  dotc = 0
  FOR i = 1 TO LEN(rhs$)
    ch$ = MID$(rhs$, i, 1)
    a = ASC(ch$)
    IF ch$ = "." THEN
      dotc = dotc + 1
    ELSEIF ch$ = "-" AND i = 1 THEN
      a = a
    ELSEIF a < 48 OR a > 57 THEN
      isNum = 0
    END IF
  NEXT i
  IF dotc > 1 THEN isNum = 0
  IF rhs$ = "-" OR rhs$ = "." THEN isNum = 0
RETURN

' ---------------------------------------------------------------------------
' Recalc all formula cells; up to 6 passes so chained formulas settle.
' (A demo-grade fixed-point iteration; circular refs just stabilize.)
' ---------------------------------------------------------------------------
Recalc:
  FOR pass = 1 TO 6
    FOR r = 1 TO NROWS
      FOR c = 1 TO NCOLS
        ' kind 4 = formula, kind 3 = formula that errored (retry each pass)
        IF kind(r, c) = 4 OR kind(r, c) = 3 THEN
          IF LEN(raw$(r, c)) > 0 THEN
            evalExpr$ = raw$(r, c)
            IF LEFT$(evalExpr$, 1) = "=" THEN evalExpr$ = MID$(evalExpr$, 2)
            GOSUB EvalFormula
            IF evalErr <> 0 THEN
              kind(r, c) = 3
            ELSE
              kind(r, c) = 4
              cv(r, c) = evalResult
            END IF
          END IF
        END IF
      NEXT c
    NEXT r
  NEXT pass
RETURN

' ---------------------------------------------------------------------------
EvalFormula:
  evalErr = 0
  evalResult = 0
  exprS$ = UCASE$(evalExpr$)
  GOSUB ExpandRanges
  IF evalErr <> 0 THEN RETURN
  GOSUB ShuntEval
RETURN

' ---------------------------------------------------------------------------
' Expand all NAME(range) aggregates in exprS$ to numeric values.
' ---------------------------------------------------------------------------
ExpandRanges:
  changed = -1
  WHILE changed <> 0
    changed = 0
    GOSUB FindFunc
    IF fpos > 0 THEN
      op = INSTR(fpos, exprS$, "(")
      cp = INSTR(op + 1, exprS$, ")")
      IF op = 0 OR cp = 0 THEN
        evalErr = 1
      ELSE
        rangeArg$ = MID$(exprS$, op + 1, cp - op - 1)
        GOSUB AggRange
        IF evalErr = 0 THEN
          before$ = LEFT$(exprS$, fpos - 1)
          after$ = MID$(exprS$, cp + 1)
          fmtIn = aggResult
          GOSUB FmtNum
          exprS$ = before$ + "(" + fmtOut$ + ")" + after$
          changed = -1
        END IF
      END IF
    END IF
  WEND
RETURN

' ---------------------------------------------------------------------------
' Find first range-function keyword in exprS$. Sets aggName$, fpos (0 none).
' ---------------------------------------------------------------------------
FindFunc:
  fpos = 0
  aggName$ = ""
  bestPos = 999999
  GOSUB TfSum
  GOSUB TfAverage
  GOSUB TfAvg
  GOSUB TfMin
  GOSUB TfMax
  GOSUB TfCount
  IF bestPos < 999999 THEN
    fpos = bestPos
    aggName$ = bestName$
  END IF
RETURN

TfSum:
  tp = INSTR(exprS$, "SUM(")
  IF tp > 0 AND tp < bestPos THEN
    bestPos = tp
    bestName$ = "SUM"
  END IF
RETURN
TfAverage:
  tp = INSTR(exprS$, "AVERAGE(")
  IF tp > 0 AND tp < bestPos THEN
    bestPos = tp
    bestName$ = "AVG"
  END IF
RETURN
TfAvg:
  tp = INSTR(exprS$, "AVG(")
  IF tp > 0 AND tp < bestPos THEN
    bestPos = tp
    bestName$ = "AVG"
  END IF
RETURN
TfMin:
  tp = INSTR(exprS$, "MIN(")
  IF tp > 0 AND tp < bestPos THEN
    bestPos = tp
    bestName$ = "MIN"
  END IF
RETURN
TfMax:
  tp = INSTR(exprS$, "MAX(")
  IF tp > 0 AND tp < bestPos THEN
    bestPos = tp
    bestName$ = "MAX"
  END IF
RETURN
TfCount:
  tp = INSTR(exprS$, "COUNT(")
  IF tp > 0 AND tp < bestPos THEN
    bestPos = tp
    bestName$ = "COUNT"
  END IF
RETURN

' ---------------------------------------------------------------------------
' Aggregate over rectangular range rangeArg$ using aggName$.
' ---------------------------------------------------------------------------
AggRange:
  cp2 = INSTR(rangeArg$, ":")
  IF cp2 = 0 THEN
    evalErr = 1
    RETURN
  END IF
  a$ = LEFT$(rangeArg$, cp2 - 1)
  b$ = MID$(rangeArg$, cp2 + 1)
  parseRef$ = a$
  refRow = 0
  refCol = 0
  GOSUB ParseRef
  ar = refRow
  ac = refCol
  parseRef$ = b$
  refRow = 0
  refCol = 0
  GOSUB ParseRef
  br = refRow
  bc = refCol
  IF ar = 0 OR br = 0 THEN
    evalErr = 1
    RETURN
  END IF
  IF ar > br THEN
    t = ar
    ar = br
    br = t
  END IF
  IF ac > bc THEN
    t = ac
    ac = bc
    bc = t
  END IF
  sumv = 0
  cntv = 0
  minv = 0
  maxv = 0
  haveMM = 0
  FOR rr = ar TO br
    FOR cc = ac TO bc
      IF kind(rr, cc) = 1 OR kind(rr, cc) = 4 THEN
        v = cv(rr, cc)
        sumv = sumv + v
        cntv = cntv + 1
        IF haveMM = 0 THEN
          minv = v
          maxv = v
          haveMM = -1
        ELSE
          IF v < minv THEN minv = v
          IF v > maxv THEN maxv = v
        END IF
      END IF
    NEXT cc
  NEXT rr
  SELECT CASE aggName$
    CASE "SUM"
      aggResult = sumv
    CASE "AVG"
      IF cntv = 0 THEN
        aggResult = 0
      ELSE
        aggResult = sumv / cntv
      END IF
    CASE "MIN"
      aggResult = minv
    CASE "MAX"
      aggResult = maxv
    CASE "COUNT"
      aggResult = cntv
    CASE ELSE
      evalErr = 1
  END SELECT
RETURN

' ---------------------------------------------------------------------------
' Shunting-yard evaluator over exprS$ (range-expanded, UCASE$).
' Operator codes: 1 + 2 - 3 * 4 / 5 ^ 6 unary- 7 (
' ---------------------------------------------------------------------------
ShuntEval:
  vtop = 0
  otop = 0
  expectVal = -1
  i = 1
  n = LEN(exprS$)
  WHILE i <= n
    ch$ = MID$(exprS$, i, 1)
    IF ch$ = " " THEN
      i = i + 1
    ELSEIF (ch$ >= "0" AND ch$ <= "9") OR ch$ = "." THEN
      GOSUB ScanNumber
      vtop = vtop + 1
      vstack(vtop) = numTok
      i = numEnd
      expectVal = 0
    ELSEIF ch$ >= "A" AND ch$ <= "Z" THEN
      GOSUB ScanRef
      IF refErr <> 0 THEN
        evalErr = 1
      ELSE
        vtop = vtop + 1
        vstack(vtop) = refVal
        i = refEnd
        expectVal = 0
      END IF
    ELSEIF ch$ = "(" THEN
      otop = otop + 1
      ostack(otop) = 7
      i = i + 1
      expectVal = -1
    ELSEIF ch$ = ")" THEN
      GOSUB PopUntilParen
      i = i + 1
      expectVal = 0
    ELSE
      opc = 0
      IF ch$ = "+" THEN opc = 1
      IF ch$ = "-" THEN opc = 2
      IF ch$ = "*" THEN opc = 3
      IF ch$ = "/" THEN opc = 4
      IF ch$ = "^" THEN opc = 5
      IF opc = 0 THEN
        evalErr = 1
      ELSE
        IF opc = 2 AND expectVal <> 0 THEN opc = 6
        GOSUB PushOp
        i = i + 1
        expectVal = -1
      END IF
    END IF
    IF evalErr <> 0 THEN i = n + 1
  WEND
  IF evalErr = 0 THEN
    WHILE otop > 0
      IF ostack(otop) = 7 THEN
        evalErr = 1
        otop = 0
      ELSE
        GOSUB ApplyTop
        IF evalErr <> 0 THEN otop = 0
      END IF
    WEND
  END IF
  IF evalErr = 0 THEN
    IF vtop <> 1 THEN
      evalErr = 1
    ELSE
      evalResult = vstack(1)
    END IF
  END IF
RETURN

' Scan a number at i -> numTok, numEnd.
ScanNumber:
  e2 = i
  scanning = -1
  WHILE scanning <> 0
    IF e2 > n THEN
      scanning = 0
    ELSE
      cc$ = MID$(exprS$, e2, 1)
      IF (cc$ >= "0" AND cc$ <= "9") OR cc$ = "." THEN
        e2 = e2 + 1
      ELSE
        scanning = 0
      END IF
    END IF
  WEND
  numStr$ = MID$(exprS$, i, e2 - i)
  numTok = VAL(numStr$)
  numEnd = e2
RETURN

' Scan a cell ref at i -> refVal, refEnd, refErr.
ScanRef:
  refErr = 0
  letter2$ = MID$(exprS$, i, 1)
  e3 = i + 1
  scanning = -1
  WHILE scanning <> 0
    IF e3 > n THEN
      scanning = 0
    ELSE
      cc2$ = MID$(exprS$, e3, 1)
      IF cc2$ >= "0" AND cc2$ <= "9" THEN
        e3 = e3 + 1
      ELSE
        scanning = 0
      END IF
    END IF
  WEND
  digits2$ = MID$(exprS$, i + 1, e3 - i - 1)
  IF LEN(digits2$) = 0 THEN
    refErr = 1
    RETURN
  END IF
  la = ASC(letter2$)
  IF la < 65 OR la > 64 + NCOLS THEN
    refErr = 1
    RETURN
  END IF
  rr2 = VAL(digits2$)
  IF rr2 < 1 OR rr2 > NROWS THEN
    refErr = 1
    RETURN
  END IF
  cc3 = la - 64
  IF kind(rr2, cc3) = 1 OR kind(rr2, cc3) = 4 THEN
    refVal = cv(rr2, cc3)
  ELSEIF kind(rr2, cc3) = 3 THEN
    refErr = 1
    RETURN
  ELSE
    refVal = 0
  END IF
  refEnd = e3
RETURN

' Pop/apply until matching '('.
PopUntilParen:
  done7 = 0
  WHILE done7 = 0
    IF otop = 0 THEN
      evalErr = 1
      done7 = -1
    ELSEIF ostack(otop) = 7 THEN
      otop = otop - 1
      done7 = -1
    ELSE
      GOSUB ApplyTop
      IF evalErr <> 0 THEN done7 = -1
    END IF
  WEND
RETURN

' Push operator opc honoring precedence/associativity.
PushOp:
  pushed = 0
  WHILE pushed = 0
    IF otop = 0 THEN
      pushed = -1
    ELSE
      top = ostack(otop)
      IF top = 7 THEN
        pushed = -1
      ELSE
        savedOpc = opc
        opc = savedOpc
        GOSUB PrecCur
        curP = precVal
        opc = top
        GOSUB PrecCur
        topP = precVal
        opc = savedOpc
        rightA = 0
        IF opc = 5 OR opc = 6 THEN rightA = -1
        IF rightA <> 0 THEN
          IF topP > curP THEN
            GOSUB ApplyTop
          ELSE
            pushed = -1
          END IF
        ELSE
          IF topP >= curP THEN
            GOSUB ApplyTop
          ELSE
            pushed = -1
          END IF
        END IF
        IF evalErr <> 0 THEN pushed = -1
      END IF
    END IF
  WEND
  otop = otop + 1
  ostack(otop) = opc
RETURN

' Precedence of opc -> precVal.
PrecCur:
  SELECT CASE opc
    CASE 1, 2
      precVal = 1
    CASE 3, 4
      precVal = 2
    CASE 5
      precVal = 3
    CASE 6
      precVal = 4
    CASE ELSE
      precVal = 0
  END SELECT
RETURN

' Apply the top operator to the value stack.
ApplyTop:
  oc = ostack(otop)
  otop = otop - 1
  IF oc = 6 THEN
    IF vtop < 1 THEN
      evalErr = 1
      RETURN
    END IF
    vstack(vtop) = -vstack(vtop)
    RETURN
  END IF
  IF vtop < 2 THEN
    evalErr = 1
    RETURN
  END IF
  b = vstack(vtop)
  vtop = vtop - 1
  a = vstack(vtop)
  SELECT CASE oc
    CASE 1
      vstack(vtop) = a + b
    CASE 2
      vstack(vtop) = a - b
    CASE 3
      vstack(vtop) = a * b
    CASE 4
      IF b = 0 THEN
        evalErr = 1
      ELSE
        vstack(vtop) = a / b
      END IF
    CASE 5
      vstack(vtop) = a ^ b
    CASE ELSE
      evalErr = 1
  END SELECT
RETURN

' ---------------------------------------------------------------------------
' Load a small demo budget sheet.
' ---------------------------------------------------------------------------
LoadDemo:
  GOSUB ClearAll
  curRow = 1 : curCol = 1 : rhs$ = "Month"   : GOSUB SetCell
  curRow = 1 : curCol = 2 : rhs$ = "Income"  : GOSUB SetCell
  curRow = 1 : curCol = 3 : rhs$ = "Expense" : GOSUB SetCell
  curRow = 1 : curCol = 4 : rhs$ = "Net"     : GOSUB SetCell
  curRow = 2 : curCol = 1 : rhs$ = "Jan"     : GOSUB SetCell
  curRow = 2 : curCol = 2 : rhs$ = "1000"    : GOSUB SetCell
  curRow = 2 : curCol = 3 : rhs$ = "600"     : GOSUB SetCell
  curRow = 2 : curCol = 4 : rhs$ = "=B2-C2"  : GOSUB SetCell
  curRow = 3 : curCol = 1 : rhs$ = "Feb"     : GOSUB SetCell
  curRow = 3 : curCol = 2 : rhs$ = "1200"    : GOSUB SetCell
  curRow = 3 : curCol = 3 : rhs$ = "750"     : GOSUB SetCell
  curRow = 3 : curCol = 4 : rhs$ = "=B3-C3"  : GOSUB SetCell
  curRow = 4 : curCol = 1 : rhs$ = "Mar"     : GOSUB SetCell
  curRow = 4 : curCol = 2 : rhs$ = "900"     : GOSUB SetCell
  curRow = 4 : curCol = 3 : rhs$ = "950"     : GOSUB SetCell
  curRow = 4 : curCol = 4 : rhs$ = "=B4-C4"  : GOSUB SetCell
  curRow = 6 : curCol = 1 : rhs$ = "Total"   : GOSUB SetCell
  curRow = 6 : curCol = 2 : rhs$ = "=SUM(B2:B4)" : GOSUB SetCell
  curRow = 6 : curCol = 3 : rhs$ = "=SUM(C2:C4)" : GOSUB SetCell
  curRow = 6 : curCol = 4 : rhs$ = "=B6-C6"  : GOSUB SetCell
  curRow = 7 : curCol = 1 : rhs$ = "Avg"     : GOSUB SetCell
  curRow = 7 : curCol = 2 : rhs$ = "=AVG(B2:B4)" : GOSUB SetCell
  curRow = 7 : curCol = 4 : rhs$ = "=MAX(D2:D4)" : GOSUB SetCell
  curRow = 1
  curCol = 1
RETURN

' ---------------------------------------------------------------------------
' Help panel; waits for ENTER (one LINE INPUT) to return.
' ---------------------------------------------------------------------------
ShowHelp:
  CLS
  COLOR 15, 1
  LOCATE 1, 1
  PRINT SPACE$(78);
  LOCATE 1, 2
  PRINT "MINICALC - HELP";
  COLOR 14, 0
  LOCATE 3, 2  : PRINT "Cell editing:"
  COLOR 7, 0
  LOCATE 4, 4  : PRINT "A1=10        store a number"
  LOCATE 5, 4  : PRINT "B1=Sales     store a label (text)"
  LOCATE 6, 4  : PRINT "C3=A1+A2*2   store a formula"
  LOCATE 7, 4  : PRINT "F1=SUM(A1:A10)  range function"
  COLOR 14, 0
  LOCATE 9, 2  : PRINT "Navigation / commands:"
  COLOR 7, 0
  LOCATE 10, 4 : PRINT "GOTO A1  or  A1   select a cell"
  LOCATE 11, 4 : PRINT "DEMO   load sample budget sheet"
  LOCATE 12, 4 : PRINT "NEW    clear all cells"
  LOCATE 13, 4 : PRINT "HELP   this screen"
  LOCATE 14, 4 : PRINT "QUIT   exit MiniCalc"
  COLOR 14, 0
  LOCATE 16, 2 : PRINT "Formula syntax:"
  COLOR 7, 0
  LOCATE 17, 4 : PRINT "operators + - * / ^  parentheses  unary -"
  LOCATE 18, 4 : PRINT "cell refs A1..F10 (case-insensitive)"
  LOCATE 19, 4 : PRINT "ranges SUM AVG MIN MAX COUNT  e.g. SUM(A1:C3)"
  COLOR 11, 0
  LOCATE 22, 2 : PRINT "Press ENTER to return to the sheet...";
  COLOR 7, 0
  LINE INPUT dummy$
RETURN
