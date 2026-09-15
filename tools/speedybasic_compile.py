#!/usr/bin/env python3
"""Turbo BASIC host frontend and native-C emitter for os8088.

The frontend deliberately has no dependency on the web implementation.  It
turns source into a small, serialisable IR, links structured control flow, and
is importable by unit tests and later native backends.  The C backend emits a
bounded native state machine for numeric expressions, arrays, structured and
unstructured control flow, graphics, and hardware-facing statements.  It
reports every instruction it cannot yet lower with its BASIC source line.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Iterable, Optional, Sequence


class CompileError(Exception):
    def __init__(self, message: str, line: int = 0, col: int = 0):
        super().__init__(message)
        self.message, self.line, self.col = message, line, col


@dataclass(frozen=True)
class Token:
    kind: str
    text: str
    line: int
    col: int
    value: Any = None


@dataclass
class Expr:
    op: str
    value: Any = None
    args: list["Expr"] = field(default_factory=list)
    line: int = 0


@dataclass
class Statement:
    op: str
    line: int
    args: list[Any] = field(default_factory=list)
    jump: int = -1
    alt: int = -1
    source: str = ""


@dataclass
class Procedure:
    name: str
    kind: str
    entry: int
    end: int = -1
    params: list[str] = field(default_factory=list)


@dataclass
class Program:
    statements: list[Statement]
    labels: dict[str, int]
    procedures: dict[str, Procedure]
    data: list[Any]

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


_SUFFIXES = "$%!&#"
_TWO_OPS = {"<=", ">=", "<>", "=>", "=<"}


def tokenize(source: str) -> list[Token]:
    out: list[Token] = []
    i, line, col = 0, 1, 1
    n = len(source)
    while i < n:
        c = source[i]
        if c in " \t\f":
            i += 1; col += 1; continue
        if c in "\r\n":
            if c == "\r" and i + 1 < n and source[i + 1] == "\n": i += 1
            out.append(Token("NL", "\n", line, col))
            i += 1; line += 1; col = 1; continue
        if c == "'":
            while i < n and source[i] not in "\r\n": i += 1; col += 1
            continue
        if c == '"':
            start, startcol = i, col
            i += 1; col += 1; chars: list[str] = []
            while i < n and source[i] not in "\r\n":
                if source[i] == '"':
                    if i + 1 < n and source[i + 1] == '"':
                        chars.append('"'); i += 2; col += 2; continue
                    i += 1; col += 1; break
                chars.append(source[i]); i += 1; col += 1
            else:
                raise CompileError("unterminated string", line, startcol)
            out.append(Token("STR", source[start:i], line, startcol, "".join(chars)))
            continue
        if c == "&" and i + 1 < n and source[i + 1].upper() in "HO":
            start, startcol, base = i, col, 16 if source[i + 1].upper() == "H" else 8
            i += 2; col += 2
            j = i
            valid = "0123456789ABCDEF" if base == 16 else "01234567"
            while i < n and source[i].upper() in valid: i += 1; col += 1
            if i == j: raise CompileError("malformed based number", line, startcol)
            text = source[start:i]
            if i < n and source[i] in "%&!#": text += source[i]; i += 1; col += 1
            out.append(Token("NUM", text, line, startcol, int(source[j:i - (1 if text[-1:] in "%&!#" else 0)], base)))
            continue
        if c.isdigit() or (c == "." and i + 1 < n and source[i + 1].isdigit()):
            start, startcol = i, col
            if c != ".":
                while i < n and source[i].isdigit(): i += 1; col += 1
            if i < n and source[i] == ".":
                i += 1; col += 1
                while i < n and source[i].isdigit(): i += 1; col += 1
            if i < n and source[i].upper() in "ED":
                i += 1; col += 1
                if i < n and source[i] in "+-": i += 1; col += 1
                if i >= n or not source[i].isdigit(): raise CompileError("malformed exponent", line, startcol)
                while i < n and source[i].isdigit(): i += 1; col += 1
            if i < n and source[i] in "%&!#": i += 1; col += 1
            text = source[start:i]
            raw = text[:-1] if text[-1:] in "%&!#" else text
            out.append(Token("NUM", text, line, startcol, float(raw.replace("D", "E").replace("d", "e")) if any(x in raw.upper() for x in ".E") else int(raw)))
            continue
        if c.isalpha() or c == "_" or (c == "$" and i + 1 < n and source[i + 1].isalpha()):
            start, startcol = i, col
            while i < n and (source[i].isalnum() or source[i] in "_."): i += 1; col += 1
            if i < n and source[i] in _SUFFIXES: i += 1; col += 1
            text = source[start:i].upper()
            out.append(Token("ID", text, line, startcol, text))
            continue
        two = source[i:i + 2]
        if two in _TWO_OPS:
            out.append(Token("OP", "<=" if two == "=<" else ">=" if two == "=>" else two, line, col))
            i += 2; col += 2; continue
        if c == "\\" and i + 1 < n and source[i + 1] == "\\":
            out.append(Token("OP", "\\", line, col)); i += 2; col += 2; continue
        kind = "PUNC" if c in "(),;:" else "OP"
        if c in "+-*/\\^=<>#":
            kind = "OP"
        elif c not in "(),;:":
            raise CompileError("unexpected character %r" % c, line, col)
        out.append(Token(kind, c, line, col))
        i += 1; col += 1
    out.append(Token("EOF", "", line, col))
    return out


_PREC = {"IMP": 1, "EQV": 2, "XOR": 3, "OR": 4, "AND": 5,
         "=": 6, "<>": 6, "<": 6, ">": 6, "<=": 6, ">=": 6,
         "+": 7, "-": 7, "*": 8, "/": 8, "\\": 8, "MOD": 8, "^": 9}


class ExprParser:
    def __init__(self, tokens: Sequence[Token]): self.t = list(tokens); self.i = 0
    def peek(self) -> Optional[Token]: return self.t[self.i] if self.i < len(self.t) else None
    def take(self) -> Token:
        t = self.peek()
        if t is None: raise CompileError("expression expected")
        self.i += 1; return t
    def parse(self, minprec: int = 0) -> Expr:
        t = self.take()
        if t.kind == "NUM": left = Expr("const", {"text": t.text, "value": t.value}, line=t.line)
        elif t.kind == "STR": left = Expr("string", t.value, line=t.line)
        elif t.text in ("+", "-") or (t.kind == "ID" and t.text == "NOT"):
            left = Expr("unary", t.text, [self.parse(9)], t.line)
        elif t.text == "(":
            left = self.parse()
            if not self.peek() or self.take().text != ")": raise CompileError("missing )", t.line, t.col)
        elif t.kind == "ID":
            if self.peek() and self.peek().text == "(":
                self.take(); args: list[Expr] = []
                if not self.peek() or self.peek().text != ")":
                    while True:
                        args.append(self.parse())
                        if self.peek() and self.peek().text == ",": self.take(); continue
                        break
                if not self.peek() or self.take().text != ")": raise CompileError("missing )", t.line, t.col)
                left = Expr("call", t.text, args, t.line)
            else: left = Expr("var", t.text, line=t.line)
        else: raise CompileError("expression expected", t.line, t.col)
        while self.peek():
            p = self.peek(); op = p.text if p.kind == "OP" else p.text if p.kind == "ID" else ""
            op = op.upper()
            prec = _PREC.get(op, -1)
            if prec < minprec: break
            self.take()
            right = self.parse(prec if op == "^" else prec + 1)
            left = Expr("binary", op, [left, right], p.line)
        return left


def parse_expr(tokens: Sequence[Token]) -> Expr:
    if not tokens: raise CompileError("expression expected")
    p = ExprParser(tokens); e = p.parse()
    if p.peek(): raise CompileError("unexpected token %s in expression" % p.peek().text, p.peek().line, p.peek().col)
    return e


def _split(tokens: Sequence[Token], delim: str = ",") -> list[list[Token]]:
    out: list[list[Token]] = [[]]; depth = 0
    for t in tokens:
        if t.text == "(": depth += 1
        elif t.text == ")": depth -= 1
        if depth == 0 and t.text == delim: out.append([])
        else: out[-1].append(t)
    return out


def _find(tokens: Sequence[Token], word: str) -> int:
    depth = 0
    for i, t in enumerate(tokens):
        if t.text == "(": depth += 1
        elif t.text == ")": depth -= 1
        elif depth == 0 and t.text == word: return i
    return -1


_KNOWN = {
    "BEEP", "BLOAD", "BSAVE", "CALL", "CHAIN", "CHDIR", "CIRCLE", "CLEAR",
    "CASE", "CLOSE", "CLS", "COLOR", "COMMON", "DATA", "DECR", "DEF", "DEFDBL",
    "DEFINT", "DEFLNG", "DEFSNG", "DEFSTR", "DEF", "DELAY", "DIM", "DO",
    "DRAW", "ELSE", "ELSEIF", "END", "ENVIRON", "ERASE", "ERROR", "EXIT",
    "FIELD", "FILES", "FOR", "GET", "GOSUB", "GOTO", "IF", "INCR", "INPUT",
    "IOCTL", "KEY", "KILL", "LET", "LINE", "LOCAL", "LOCATE", "LOOP", "LPRINT",
    "LSET", "MEMSET", "MID$", "MKDIR", "MTIMER", "NAME", "NEXT", "ON", "OPEN",
    "OPTION", "OUT", "PAINT", "PALETTE", "PEN", "PLAY", "POKE", "PRESET",
    "PRINT", "PSET", "PUT", "RANDOMIZE", "READ", "REDIM", "REG", "REM", "RESET",
    "RESTORE", "RESUME", "RETURN", "RMDIR", "RSET", "RUN", "SCREEN", "SEEK",
    "SELECT", "SHARED", "SHELL", "SLEEP", "SOUND", "STATIC", "STOP", "STRIG",
    "SUB", "SWAP", "SYSTEM", "TIMER", "VIEW", "WAIT", "WEND", "WHILE", "WIDTH",
    "WINDOW", "WRITE", "$DYNAMIC", "$STATIC", "$INLINE", "$EVENT", "$SEGMENT",
}


class Frontend:
    def __init__(self, source: str, filename: str = "<input>"):
        self.source, self.filename = source, filename
        self.statements: list[Statement] = []
        self.labels: dict[str, int] = {}
        self.procs: dict[str, Procedure] = {}
        self.data: list[Any] = []

    def compile(self) -> Program:
        toks = tokenize(self.source); lines: list[list[Token]] = []; cur: list[Token] = []
        for t in toks:
            if t.kind in ("NL", "EOF"):
                if cur: lines.append(cur); cur = []
            else: cur.append(t)
        for line in lines: self._line(line)
        self._link()
        return Program(self.statements, self.labels, self.procs, self.data)

    def _line(self, ts: list[Token]) -> None:
        if ts and ts[0].kind == "NUM" and isinstance(ts[0].value, int):
            self.labels[str(ts[0].value)] = len(self.statements); ts = ts[1:]
        if len(ts) >= 2 and ts[0].kind == "ID" and ts[1].text == ":":
            self.labels[ts[0].text] = len(self.statements); ts = ts[2:]
        if not ts: return
        parts = _split(ts, ":"); i = 0
        while i < len(parts):
            part = parts[i]
            if not part: i += 1; continue
            if part[0].text == "IF":
                joined = part[:]
                for rest in parts[i + 1:]: joined += [Token("PUNC", ":", part[0].line, 0)] + rest
                self._stmt(joined); return
            self._stmt(part); i += 1

    def _stmt_list(self, ts: Sequence[Token]) -> list[Statement]:
        saved = self.statements; self.statements = []
        for p in _split(ts, ":"):
            if p: self._stmt(p)
        result, self.statements = self.statements, saved
        return result

    def _stmt(self, ts: list[Token]) -> None:
        line = ts[0].line; first = ts[0].text
        if first == "REM": self.statements.append(Statement("REM", line)); return
        if first not in _KNOWN:
            eq = _find(ts, "=")
            if ts[0].kind == "ID" and eq < 0:
                name,args=self._call(ts);self.statements.append(Statement("CALL",line,[name,args],source=first));return
            if ts[0].kind != "ID" or eq < 1: raise CompileError("unknown statement %s" % first, line, ts[0].col)
            lhs, rhs = ts[:eq], ts[eq + 1:]
            self.statements.append(Statement("STORE", line, [self._lvalue(lhs), parse_expr(rhs)], source=first)); return
        if first == "LET":
            eq = _find(ts, "=")
            if eq < 2: raise CompileError("LET requires =", line, ts[0].col)
            self.statements.append(Statement("STORE", line, [self._lvalue(ts[1:eq]), parse_expr(ts[eq + 1:])], source="LET")); return
        if first == "IF":
            k = _find(ts, "THEN")
            if k < 0: raise CompileError("IF without THEN", line, ts[0].col)
            tail = ts[k + 1:]
            if not tail: self.statements.append(Statement("IF_BEGIN", line, [parse_expr(ts[1:k])], source="IF")); return
            e = _find(tail, "ELSE")
            yes, no = (tail, []) if e < 0 else (tail[:e], tail[e + 1:])
            self.statements.append(Statement("IF_INLINE", line, [parse_expr(ts[1:k]), self._stmt_list(yes), self._stmt_list(no)], source="IF")); return
        if first == "ELSEIF":
            k = _find(ts, "THEN")
            if k < 0: raise CompileError("ELSEIF without THEN", line, ts[0].col)
            self.statements.append(Statement("ELSEIF", line, [parse_expr(ts[1:k])], source=first)); return
        if first == "FOR":
            eq, to, step = _find(ts, "="), _find(ts, "TO"), _find(ts, "STEP")
            if min(eq, to) < 0: raise CompileError("malformed FOR", line, ts[0].col)
            end = step if step >= 0 else len(ts)
            args = [self._lvalue(ts[1:eq]), parse_expr(ts[eq + 1:to]), parse_expr(ts[to + 1:end]), parse_expr(ts[step + 1:]) if step >= 0 else Expr("const", {"text":"1","value":1}, line=line)]
            self.statements.append(Statement("FOR", line, args, source=first)); return
        if first in ("WHILE",): self.statements.append(Statement(first, line, [parse_expr(ts[1:])], source=first)); return
        if first == "DO":
            args=[] if len(ts)==1 else [ts[1].text,parse_expr(ts[2:])]
            if args and args[0] not in ("WHILE","UNTIL"):raise CompileError("DO expects WHILE or UNTIL",line)
            self.statements.append(Statement("DO",line,args,source=first));return
        if first == "LOOP":
            args = [] if len(ts) == 1 else [ts[1].text, parse_expr(ts[2:])]
            self.statements.append(Statement("LOOP", line, args, source=first)); return
        if first == "SELECT":
            start = 2 if len(ts)>1 and ts[1].text == "CASE" else 1
            self.statements.append(Statement("SELECT", line, [parse_expr(ts[start:])], source=first)); return
        if first == "CASE":
            if len(ts)>1 and ts[1].text == "ELSE": args = ["ELSE"]
            else: args = [self._case_part(x) for x in _split(ts[1:])]
            self.statements.append(Statement("CASE", line, args, source=first)); return
        if first == "DATA":
            vals = []
            for p in _split(ts[1:]):
                if len(p)==1 and p[0].kind=="STR": v=p[0].value
                elif p: v="".join(x.text for x in p)
                else: v=""
                vals.append(v); self.data.append(v)
            self.statements.append(Statement("DATA", line, vals, source=first)); return
        if first in ("DIM","REDIM"):
            decls=[]
            for p in _split(ts[1:]):
                if not p or p[0].kind!="ID":raise CompileError("array name expected",line)
                # Turbo BASIC permits scalar declarations in the same DIM as
                # arrays (for example ``DIM A%(10), S$``).  Keep the scalar in
                # ordinary variable storage and reserve DIM IR for arrays.
                if len(p)==1:
                    self.statements.append(Statement("DECLARE",line,[p[0].text],source=first))
                    continue
                if len(p)<4 or p[1].text!="(" or p[-1].text!=")":raise CompileError("array bounds expected",line)
                bounds=[parse_expr(x) for x in _split(p[2:-1])]
                decls.append([p[0].text,bounds])
            self.statements.append(Statement(first,line,decls,source=first));return
        if first == "END" and len(ts)>1:
            op={"IF":"END_IF","SUB":"END_SUB","SELECT":"END_SELECT","DEF":"END_DEF"}.get(ts[1].text,"END")
            self.statements.append(Statement(op,line,source=first));return
        if first in ("SUB",):
            name, params = self._decl(ts[1:]); p=Procedure(name,"SUB",len(self.statements),params=params);self.procs[name]=p
            self.statements.append(Statement("SUB",line,[name,params],source=first));return
        if first == "DEF" and len(ts)>1 and ts[1].text.startswith("FN"):
            name, params = self._decl(ts[1:]); p=Procedure(name,"DEF",len(self.statements),params=params);self.procs[name]=p
            self.statements.append(Statement("DEF_FN",line,[name,params,ts],source=first));return
        if first == "CALL":
            name, args = self._call(ts[1:]);self.statements.append(Statement("CALL",line,[name,args],source=first));return
        if first in ("REG","OUT","POKE","DELAY"):
            parts=_split(ts[1:]);args=[parse_expr(p) for p in parts if p]
            self.statements.append(Statement(first,line,args,source=first));return
        if first=="DEF" and len(ts)>1 and ts[1].text=="SEG":
            eq=_find(ts,"=");args=[] if eq<0 or eq+1>=len(ts) else [parse_expr(ts[eq+1:])]
            self.statements.append(Statement("DEF_SEG",line,args,source=first));return
        if first in ("GOTO","GOSUB","RESTORE"):
            target = ts[1].text if len(ts)>1 else ""
            self.statements.append(Statement(first,line,[target],source=first));return
        if first in ("PSET","PRESET"):
            args=self._gfx_args(ts[1:]);self.statements.append(Statement(first,line,args,source=first));return
        if first == "LINE" and len(ts)>1 and ts[1].text != "INPUT":
            dash=_find(ts,"-"); comma_after=-1
            if dash<0: raise CompileError("LINE missing -",line,ts[0].col)
            left=ts[1:dash]; right=ts[dash+1:]; depth=0
            for j,t in enumerate(right):
                depth += (t.text=="(")-(t.text==")")
                if depth==0 and t.text==",​": comma_after=j;break
            # Split after the closing coordinate pair, then parse color/style.
            depth=0;close=len(right)-1
            for j,t in enumerate(right):
                if t.text=="(":depth+=1
                elif t.text==")":
                    depth-=1
                    if depth==0:close=j;break
            opts=_split(right[close+2:]) if close+1<len(right) and right[close+1].text=="," else []
            args=self._coord_pair(left)+self._coord_pair(right[:close+1])
            args += [parse_expr(opts[0]) if opts and opts[0] else None, opts[1][0].text if len(opts)>1 and opts[1] else "", parse_expr(opts[2]) if len(opts)>2 and opts[2] else None]
            self.statements.append(Statement("LINE",line,args,source=first));return
        # Preserve known syntax as typed tokens. Backend handlers can consume
        # it incrementally without losing source-level distinctions.
        self.statements.append(Statement(first,line,[{"kind":t.kind,"text":t.text,"value":t.value} for t in ts[1:]],source=first))

    def _lvalue(self, ts: Sequence[Token]) -> Expr:
        if not ts or ts[0].kind!="ID": raise CompileError("assignment target expected", ts[0].line if ts else 0)
        if len(ts)==1:return Expr("var",ts[0].text,line=ts[0].line)
        return parse_expr(ts)
    def _decl(self, ts: Sequence[Token]) -> tuple[str,list[str]]:
        if not ts or ts[0].kind!="ID": raise CompileError("procedure name expected")
        name=ts[0].text; params=[]
        if len(ts)>1 and ts[1].text=="(":
            for p in _split(ts[2:-1]):
                ids=[x.text for x in p if x.kind=="ID" and x.text not in ("BYVAL","BYREF","AS")]
                if ids:params.append(ids[0])
        return name,params
    def _call(self,ts:Sequence[Token])->tuple[str,list[Expr]]:
        if not ts or ts[0].kind!="ID":raise CompileError("CALL target expected")
        rest=ts[1:]; args=[]
        if rest and rest[0].text=="(":rest=rest[1:-1]
        for p in _split(rest):
            if p:args.append(parse_expr(p))
        return ts[0].text,args
    def _coord_pair(self,ts:Sequence[Token])->list[Expr]:
        t=list(ts)
        if t and t[0].text=="STEP":t=t[1:]
        if t and t[0].text=="(":t=t[1:]
        if t and t[-1].text==")":t=t[:-1]
        p=_split(t)
        if len(p)!=2:raise CompileError("coordinate pair expected",ts[0].line if ts else 0)
        return [parse_expr(p[0]),parse_expr(p[1])]
    def _gfx_args(self,ts:Sequence[Token])->list[Any]:
        depth=0; close=-1
        for i,t in enumerate(ts):
            depth += (t.text=="(")-(t.text==")")
            if depth==0 and t.text==")":close=i;break
        if close<0:close=len(ts)-1
        args=self._coord_pair(ts[:close+1]); rest=list(ts[close+1:])
        if rest and rest[0].text==",":rest=rest[1:]
        args.append(parse_expr(rest) if rest else None);return args
    def _case_part(self,ts:Sequence[Token])->Any:
        k=_find(ts,"TO")
        return [parse_expr(ts[:k]),parse_expr(ts[k+1:])] if k>=0 else parse_expr(ts)

    def _link(self)->None:
        blocks: list[tuple[str,int]]=[]; procstack:list[str]=[]
        for i,s in enumerate(self.statements):
            if s.op in ("IF_BEGIN","FOR","WHILE","DO","SELECT"):
                blocks.append((s.op,i))
            elif s.op in ("ELSEIF","ELSE"):
                if not blocks or blocks[-1][0] not in ("IF_BEGIN","ELSEIF","ELSE"):raise CompileError(s.op+" without IF",s.line)
                self.statements[blocks[-1][1]].jump=i;blocks[-1]=(s.op,i)
            elif s.op=="END_IF":
                j=next((j for j in range(len(blocks)-1,-1,-1) if blocks[j][0] in ("IF_BEGIN","ELSEIF","ELSE")),-1)
                if j<0:raise CompileError("END IF without IF",s.line)
                _,start=blocks.pop(j);self.statements[start].jump=i+1;s.jump=start
            elif s.op=="END_SELECT":
                j=next((j for j in range(len(blocks)-1,-1,-1) if blocks[j][0]=="SELECT"),-1)
                if j<0:raise CompileError("END SELECT without SELECT",s.line)
                _,start=blocks.pop(j);self.statements[start].jump=i+1;s.jump=start
            elif s.op in ("NEXT","WEND","LOOP","CASE"):
                want={"NEXT":"FOR","WEND":"WHILE","LOOP":"DO","CASE":"SELECT"}[s.op]
                j=next((j for j in range(len(blocks)-1,-1,-1) if blocks[j][0]==want),-1)
                if j>=0 and s.op!="CASE":
                    _,start=blocks.pop(j);self.statements[start].jump=i+1;s.jump=start
            if s.op=="SUB":procstack.append(s.args[0])
            if s.op=="DEF_FN":procstack.append(s.args[0])
            if s.op in ("END_SUB","END_DEF") and procstack:self.procs[procstack.pop()].end=i
        if blocks:
            kind,pos=blocks[-1];raise CompileError("unclosed "+kind,self.statements[pos].line)
        for s in self.statements:
            if s.op in ("GOTO","GOSUB"):
                key=str(s.args[0]).upper()
                if key not in self.labels:raise CompileError("undefined target "+key,s.line)
                s.jump=self.labels[key]
            if s.op=="CALL" and s.source!="CALL" and s.args[0] not in self.procs:
                raise CompileError("unknown statement "+s.source,s.line)


def compile_source(source: str, filename: str = "<input>") -> Program:
    return Frontend(source, filename).compile()


def _cstr(s: str) -> str:
    return json.dumps(s)


def _const(e: Expr) -> Optional[int]:
    if e.op=="const":
        v=e.value["value"]
        return int(v) if float(v).is_integer() else None
    return None


class CEmitter:
    """Lower IR to a bounded, resumable native C state machine."""
    def __init__(self, program: Program, name: str):
        self.p, self.name = program, name
        self.vars: dict[str,int] = {}
        self.arrays: dict[str,int] = {}
        self.for_ids: dict[int,int] = {}
        self.errors: list[tuple[int,str]] = []
        self._inventory()

    def _var(self,name:str)->int:
        if name not in self.vars:self.vars[name]=len(self.vars)
        return self.vars[name]
    def _expr_inventory(self,e:Any)->None:
        if not isinstance(e,Expr):return
        if e.op=="var" and e.value not in ("INKEY$","TIMER","RND"):self._var(e.value)
        if e.op=="call" and e.value not in self.arrays:
            for a in e.args:self._expr_inventory(a)
        else:
            for a in e.args:self._expr_inventory(a)
    def _walk_arg(self,a:Any)->None:
        if isinstance(a,Expr):self._expr_inventory(a)
        elif isinstance(a,list):
            for x in a:self._walk_arg(x)
        elif isinstance(a,Statement):
            for x in a.args:self._walk_arg(x)
    def _inventory(self)->None:
        for s in self.p.statements:
            if s.op in ("DIM","REDIM"):
                for name,_ in s.args:
                    if name not in self.arrays:self.arrays[name]=len(self.arrays)
            for a in s.args:self._walk_arg(a)
        # Calls are array reads only when DIM declared the name.
        for i,s in enumerate(self.p.statements):
            if s.op=="FOR":self.for_ids[i]=len(self.for_ids)

    @staticmethod
    def _pc(i:int)->int:return i+1
    @staticmethod
    def _indent(lines:list[str],n:int)->list[str]:return ["    "*n+x for x in lines]
    def _fail(self,line:int,what:str)->None:self.errors.append((line,what))

    def expr(self,e:Expr,d:int=0)->list[str]:
        q: list[str]=[]
        if e.op=="const":
            v=e.value["value"]
            if isinstance(v,float) and not v.is_integer():
                raw=int(round(v*65536.0));lo=raw&65535;hi=(raw>>16)&65535
                if hi>=32768:hi-=65536
                return ["sbr_num_load(%du,%d,SBN_FIXED);"%(lo,hi)]
            v=int(v);return ["sbr_num_load(%du,%d,SBN_LONG);"%(v&65535,-1 if v<0 else (v>>16)&65535)]
        if e.op=="string":
            self._fail(e.line,"string expression")
            return ["sbr_num_int(0);"]
        if e.op=="var":
            if e.value=="INKEY$":return ["sbr_num_int(sbr_inkey());"]
            if e.value in ("TIMER","RND"):
                self._fail(e.line,e.value);return ["sbr_num_int(0);"]
            i=self._var(e.value);return ["sbr_num_load(sbp_vlo[%d],sbp_vhi[%d],sbp_vkind[%d]);"%(i,i,i)]
        if e.op=="call":
            if e.value in self.arrays:
                if len(e.args)!=1:self._fail(e.line,"only 1D arrays are lowered");return ["sbr_num_int(0);"]
                q+=self._int_expr(e.args[0],d);q+=["if(sbp_tint[%d]<0) return sbr_fail(\"array index\");"%d,
                    "if(sbr_num_array_load(sbp_abase[%d]+(unsigned)sbp_tint[%d])<0) return sbr_fail(\"array load\");"%(self.arrays[e.value],d)];return q
            if e.value=="CHR$" and len(e.args)==1:return self.expr(e.args[0],d)
            builtin={"INT":"SBR_NUM_FLOOR","FIX":"SBR_NUM_FIX","CINT":"SBR_NUM_CINT","CLNG":"SBR_NUM_CINT","CSNG":"SBR_NUM_FLOAT","CDBL":"SBR_NUM_FLOAT","SQR":"SBR_NUM_SQRT","SIN":"SBR_NUM_SIN","COS":"SBR_NUM_COS"}.get(e.value)
            if builtin and len(e.args)==1:return self.expr(e.args[0],d)+["if(sbr_num_unary(%s)) return sbr_fail(\"numeric builtin\");"%builtin]
            self._fail(e.line,"function "+e.value);return ["sbr_num_int(0);"]
        if e.op=="unary":
            q=self.expr(e.args[0],d)
            op={"-":"SBR_NUM_NEG","+":None,"NOT":"SBR_NUM_NOT"}.get(e.value)
            if op:q.append("if(sbr_num_unary(%s)) return sbr_fail(\"numeric unary\");"%op)
            return q
        if e.op=="binary":
            q=self.expr(e.args[0],d)
            q += ["sbp_tlo[%d]=sbn_alo;sbp_thi[%d]=sbn_ahi;sbp_tkind[%d]=sbn_akind;"%(d,d,d)]
            q += self.expr(e.args[1],d+1)
            ar={"+":"SBR_NUM_ADD","-":"SBR_NUM_SUB","*":"SBR_NUM_MUL","/":"SBR_NUM_FDIV","\\":"SBR_NUM_IDIV","MOD":"SBR_NUM_MOD","^":"SBR_NUM_POW","AND":"SBR_NUM_AND","OR":"SBR_NUM_OR","XOR":"SBR_NUM_XOR"}.get(e.value)
            rel={"=":"SBR_NUM_EQ","<>":"SBR_NUM_NE","<":"SBR_NUM_LT","<=":"SBR_NUM_LE",">":"SBR_NUM_GT",">=":"SBR_NUM_GE"}.get(e.value)
            if ar:q.append("if(sbr_num_binary(%s,sbp_tlo[%d],sbp_thi[%d],sbp_tkind[%d])) return sbr_fail(\"numeric binary\");"%(ar,d,d,d))
            elif e.value in ("IMP","EQV"):
                # Preserve RHS, transform the saved LHS, then combine.  BASIC
                # IMP is (NOT lhs) OR rhs; EQV is NOT(lhs XOR rhs).
                q += ["sbp_tlo[%d]=sbn_alo;sbp_thi[%d]=sbn_ahi;sbp_tkind[%d]=sbn_akind;"%(d+1,d+1,d+1),
                      "sbr_num_load(sbp_tlo[%d],sbp_thi[%d],sbp_tkind[%d]);"%(d,d,d)]
                if e.value=="IMP":q += ["if(sbr_num_unary(SBR_NUM_NOT)) return sbr_fail(\"numeric IMP\");"]
                q += ["sbp_tlo[%d]=sbn_alo;sbp_thi[%d]=sbn_ahi;sbp_tkind[%d]=sbn_akind;"%(d,d,d),
                      "sbr_num_load(sbp_tlo[%d],sbp_thi[%d],sbp_tkind[%d]);"%(d+1,d+1,d+1),
                      "if(sbr_num_binary(%s,sbp_tlo[%d],sbp_thi[%d],sbp_tkind[%d])) return sbr_fail(\"numeric bitwise\");"%("SBR_NUM_OR" if e.value=="IMP" else "SBR_NUM_XOR",d,d,d)]
                if e.value=="EQV":q += ["if(sbr_num_unary(SBR_NUM_NOT)) return sbr_fail(\"numeric EQV\");"]
            elif rel:q.append("if(sbr_num_relation(%s,sbp_tlo[%d],sbp_thi[%d],sbp_tkind[%d])) return sbr_fail(\"numeric relation\");"%(rel,d,d,d))
            else:self._fail(e.line,"operator "+e.value)
            return q
        self._fail(e.line,"expression "+e.op);return ["sbr_num_int(0);"]

    def store(self,lhs:Expr,rhs:Expr,d:int=0)->list[str]:
        q=self.expr(rhs,d)
        if lhs.op=="var":
            i=self._var(lhs.value);q += ["sbp_vlo[%d]=sbn_alo;sbp_vhi[%d]=sbn_ahi;sbp_vkind[%d]=sbn_akind;"%(i,i,i)];return q
        if lhs.op=="call" and lhs.value in self.arrays and len(lhs.args)==1:
            q += ["sbp_tlo[%d]=sbn_alo;sbp_thi[%d]=sbn_ahi;sbp_tkind[%d]=sbn_akind;"%(d,d,d)]
            q += self.expr(lhs.args[0],d+1)
            q += ["sbp_tint[%d]=sbr_num_target_int();if(sbn_err) return sbr_fail(\"array index\");"%(d+1),"if(sbp_tint[%d]<0) return sbr_fail(\"array index\");"%(d+1),"sbp_cell=sbp_abase[%d]+(unsigned)sbp_tint[%d];"%(self.arrays[lhs.value],d+1),"sbr_num_load(sbp_tlo[%d],sbp_thi[%d],sbp_tkind[%d]);"%(d,d,d),"if(sbr_num_array_store(sbp_cell)<0) return sbr_fail(\"array store\");"]
            return q
        self._fail(lhs.line,"assignment target");return q

    def _int_expr(self,e:Expr,d:int=0)->list[str]:
        return self.expr(e,d)+["sbp_tint[%d]=sbr_num_target_int();if(sbn_err) return sbr_fail(\"integer conversion\");"%d]

    def inline(self,s:Statement,d:int=0)->list[str]:
        q=[]
        if s.op=="STORE":return self.store(s.args[0],s.args[1],d)
        if s.op=="POKE" and len(s.args)>=2:
            q+=self._int_expr(s.args[0],d);q+=self._int_expr(s.args[1],d+1);q+=["sbr_poke((unsigned)sbp_tint[%d],sbp_tint[%d]);"%(d,d+1)];return q
        if s.op=="GOSUB":q += ["if(sbp_gsp>=24)return sbr_fail(\"GOSUB stack\");","sbp_gosub[sbp_gsp++]=sbp_pc+1;","sbp_pc=%d;continue;"%self._pc(s.jump)];return q
        if s.op=="GOTO":return ["sbp_pc=%d;continue;"%self._pc(s.jump)]
        if s.op=="RETURN":return ["if(!sbp_gsp)return sbr_fail(\"RETURN stack\");","sbp_pc=sbp_gosub[--sbp_gsp];continue;"]
        if s.op=="IF_INLINE":
            q+=self.expr(s.args[0],d);q+=["if(sbr_num_truth()){"]
            for x in s.args[1]:q+=self._indent(self.inline(x,d+1),1)
            if s.args[2]:
                q+=["}else{"]
                for x in s.args[2]:q+=self._indent(self.inline(x,d+1),1)
            q+=["}"];return q
        self._fail(s.line,"inline "+s.op);return q

    def stmt(self,i:int,s:Statement)->list[str]:
        nxt=self._pc(i+1);q=[]
        if s.op in ("REM","DATA","DECLARE","DEFINT","DEFLNG","DEFSNG","DEFDBL","DEFSTR","WIDTH","END_IF"):
            return ["sbp_pc=%d;break;"%nxt]
        if s.op in ("END","STOP","SYSTEM"):return ["return SBR_DONE;"]
        if s.op=="STORE":q=self.store(s.args[0],s.args[1])
        elif s.op=="IF_INLINE":q=self.inline(s)
        elif s.op=="IF_BEGIN":
            # The linker points at the next clause marker.  ELSE itself is a
            # true-branch escape, so a false IF enters the statement after it;
            # ELSEIF must execute its own condition at the marker.
            false_pc=self._pc(s.jump+1) if s.jump<len(self.p.statements) and self.p.statements[s.jump].op=="ELSE" else self._pc(s.jump)
            q=self.expr(s.args[0]);q+=["if(!sbr_num_truth())sbp_pc=%d;else sbp_pc=%d;break;"%(false_pc,nxt)];return q
        elif s.op in ("ELSE","ELSEIF"):
            if s.op=="ELSE":return ["sbp_pc=%d;break;"%self._pc(s.jump)]
            false_pc=self._pc(s.jump+1) if s.jump<len(self.p.statements) and self.p.statements[s.jump].op=="ELSE" else self._pc(s.jump)
            q=self.expr(s.args[0]);q+=["if(!sbr_num_truth())sbp_pc=%d;else sbp_pc=%d;break;"%(false_pc,nxt)];return q
        elif s.op=="FOR":
            f=self.for_ids[i];lhs,start,limit,step=s.args;q=self.store(lhs,start);q+=self.expr(limit);q+=["sbp_flo[%d]=sbn_alo;sbp_fhi[%d]=sbn_ahi;sbp_fkind[%d]=sbn_akind;"%(f,f,f)];q+=self.expr(step);q+=["sbp_slo[%d]=sbn_alo;sbp_shi[%d]=sbn_ahi;sbp_skind[%d]=sbn_akind;"%(f,f,f)];q+=self.expr(lhs);q+=["sbr_num_load(sbp_slo[%d],sbp_shi[%d],sbp_skind[%d]);"%(f,f,f),"sbp_tint[0]=sbr_num_target_int();if(sbn_err)return sbr_fail(\"FOR step\");", "sbr_num_load(sbp_flo[%d],sbp_fhi[%d],sbp_fkind[%d]);"%(f,f,f),"if(sbr_num_relation(sbp_tint[0]<0?SBR_NUM_GE:SBR_NUM_LE,sbp_vlo[%d],sbp_vhi[%d],sbp_vkind[%d]))return sbr_fail(\"FOR compare\");"%(self._var(lhs.value),self._var(lhs.value),self._var(lhs.value)),"sbp_pc=sbr_num_truth()?%d:%d;break;"%(nxt,self._pc(s.jump))];return q
        elif s.op=="NEXT":
            start=s.jump;fs=self.p.statements[start];f=self.for_ids[start];lhs=fs.args[0];vi=self._var(lhs.value)
            q=["sbr_num_load(sbp_slo[%d],sbp_shi[%d],sbp_skind[%d]);"%(f,f,f),"if(sbr_num_binary(SBR_NUM_ADD,sbp_vlo[%d],sbp_vhi[%d],sbp_vkind[%d]))return sbr_fail(\"NEXT add\");"%(vi,vi,vi),"sbp_vlo[%d]=sbn_alo;sbp_vhi[%d]=sbn_ahi;sbp_vkind[%d]=sbn_akind;"%(vi,vi,vi),"sbr_num_load(sbp_slo[%d],sbp_shi[%d],sbp_skind[%d]);"%(f,f,f),"sbp_tint[0]=sbr_num_target_int();if(sbn_err)return sbr_fail(\"NEXT step\");","sbr_num_load(sbp_flo[%d],sbp_fhi[%d],sbp_fkind[%d]);"%(f,f,f),"if(sbr_num_relation(sbp_tint[0]<0?SBR_NUM_GE:SBR_NUM_LE,sbp_vlo[%d],sbp_vhi[%d],sbp_vkind[%d]))return sbr_fail(\"NEXT compare\");"%(vi,vi,vi),"sbp_pc=sbr_num_truth()?%d:%d;break;"%(self._pc(start+1),nxt)];return q
        elif s.op=="WHILE":q=self.expr(s.args[0]);q+=["sbp_pc=sbr_num_truth()?%d:%d;break;"%(nxt,self._pc(s.jump))];return q
        elif s.op=="WEND":return ["sbp_pc=%d;break;"%self._pc(s.jump)]
        elif s.op=="DO":
            if not s.args:return ["sbp_pc=%d;break;"%nxt]
            q=self.expr(s.args[1]);test="!sbr_num_truth()" if s.args[0]=="UNTIL" else "sbr_num_truth()";q+=["sbp_pc=(%s)?%d:%d;break;"%(test,nxt,self._pc(s.jump))];return q
        elif s.op=="LOOP":
            if not s.args:return ["sbp_pc=%d;break;"%self._pc(s.jump+1)]
            q=self.expr(s.args[1]);test="!sbr_num_truth()" if s.args[0]=="UNTIL" else "sbr_num_truth()";q+=["sbp_pc=(%s)?%d:%d;break;"%(test,self._pc(s.jump+1),nxt)];return q
        elif s.op=="GOTO":return ["sbp_pc=%d;break;"%self._pc(s.jump)]
        elif s.op=="GOSUB":return ["if(sbp_gsp>=24)return sbr_fail(\"GOSUB stack\");","sbp_gosub[sbp_gsp++]=%d;"%nxt,"sbp_pc=%d;break;"%self._pc(s.jump)]
        elif s.op=="RETURN":return ["if(!sbp_gsp)return sbr_fail(\"RETURN stack\");","sbp_pc=sbp_gosub[--sbp_gsp];break;"]
        elif s.op in ("DIM","REDIM"):
            for an,bounds in s.args:
                if len(bounds)!=1:self._fail(s.line,"only 1D arrays are lowered");continue
                q+=self._int_expr(bounds[0]);q+=["if(sbp_tint[0]<0)return sbr_fail(\"array bound\");","sbp_abase[%d]=(unsigned)sbm_make((unsigned)sbp_tint[0]+1u);"%self.arrays[an],"if((int)sbp_abase[%d]<0)return sbr_fail(\"array allocation\");"%self.arrays[an]]
        elif s.op=="SCREEN":
            val=next((x.get("value") for x in s.args if isinstance(x,dict) and x.get("kind")=="NUM"),None)
            if val is None:self._fail(s.line,"dynamic SCREEN")
            else:q=["sbr_screen(%d);"%int(val)]
        elif s.op=="CLS":q=["sbr_cls();"]
        elif s.op in ("PSET","PRESET"):
            x,y,c=s.args;q+=self._int_expr(x,0);q+=self._int_expr(y,1);q+=self._int_expr(c,2) if c else ["sbp_tint[2]=%d;"%(0 if s.op=="PRESET" else 15)];q+=["sbr_pset(sbp_tint[0],sbp_tint[1],sbp_tint[2]);"]
        elif s.op=="LINE" and len(s.args)==7:
            x1,y1,x2,y2,color,style,_pattern=s.args
            for j,e in enumerate((x1,y1,x2,y2)):
                q+=self._int_expr(e,j)
            q+=self._int_expr(color,4) if color else ["sbp_tint[4]=15;"]
            if style in ("B","BF"):
                q+=["sbr_line(sbp_tint[0],sbp_tint[1],sbp_tint[2],sbp_tint[1],sbp_tint[4]);",
                    "sbr_line(sbp_tint[2],sbp_tint[1],sbp_tint[2],sbp_tint[3],sbp_tint[4]);",
                    "sbr_line(sbp_tint[2],sbp_tint[3],sbp_tint[0],sbp_tint[3],sbp_tint[4]);",
                    "sbr_line(sbp_tint[0],sbp_tint[3],sbp_tint[0],sbp_tint[1],sbp_tint[4]);"]
                if style=="BF":
                    q+=["sbp_tint[5]=sbp_tint[1];",
                        "while(sbp_tint[5]<=sbp_tint[3]){sbr_line(sbp_tint[0],sbp_tint[5],sbp_tint[2],sbp_tint[5],sbp_tint[4]);sbp_tint[5]++;}"]
            else:q+=["sbr_line(sbp_tint[0],sbp_tint[1],sbp_tint[2],sbp_tint[3],sbp_tint[4]);"]
        elif s.op=="LINE":self._fail(s.line,"LINE INPUT")
        elif s.op=="OUT":q+=self._two_int_call("sbr_out",s.args)
        elif s.op=="REG":q+=self._two_int_call("sbr_reg",s.args)
        elif s.op=="POKE":q+=self._two_int_call("sbr_poke",s.args)
        elif s.op=="DEF_SEG":
            if s.args:q+=self._int_expr(s.args[0]);q+=["sbr_defseg((unsigned)sbp_tint[0]);"]
            else:q=["sbr_defseg(0);"]
        elif s.op=="DELAY":q+=self._int_expr(s.args[0]);q+=["sbr_delay((unsigned)sbp_tint[0]);"]
        elif s.op=="CALL" and s.args[0]=="INTERRUPT" and s.args[1]:q+=self._int_expr(s.args[1][0]);q+=["sbr_interrupt(sbp_tint[0]);"]
        elif s.op=="PRINT":
            text=next((x.get("value") for x in s.args if isinstance(x,dict) and x.get("kind")=="STR"),None)
            if text is not None:q=["sbr_print_str(%s);"%_cstr(text),"sbr_print_nl();"]
            else:self._fail(s.line,"PRINT expression")
        else:self._fail(s.line,s.op)
        q += ["sbp_pc=%d;break;"%nxt];return q

    def _two_int_call(self,name:str,args:list[Expr])->list[str]:
        if len(args)<2:self._fail(args[0].line if args else 0,name+" arguments");return []
        q=self._int_expr(args[0],0);q+=self._int_expr(args[1],1);q+=["%s(sbp_tint[0],sbp_tint[1]);"%name];return q

    def emit(self)->str:
        title=re.sub(r"[^A-Za-z0-9 _.-]","_",self.name);nv=max(1,len(self.vars));na=max(1,len(self.arrays));nf=max(1,len(self.for_ids))
        h=["/* generated by speedybasic_compile.py */",'#define SB_PROGRAM_TITLE '+_cstr(title),"int sbp_program(int budget);",'#include "speedybasic/compiler/numrt.c"','#include "speedybasic/compiler/runtime.c"',"","static unsigned sbp_pc;","static unsigned sbp_vlo[%d],sbp_abase[%d];"%(nv,na),"static int sbp_vhi[%d],sbp_vkind[%d];"%(nv,nv),"static unsigned sbp_tlo[16];","static int sbp_thi[16],sbp_tkind[16],sbp_tint[16];","static unsigned sbp_flo[%d],sbp_slo[%d];"%(nf,nf),"static int sbp_fhi[%d],sbp_fkind[%d],sbp_shi[%d],sbp_skind[%d];"%(nf,nf,nf,nf),"static unsigned sbp_gosub[24],sbp_cell;","static int sbp_gsp;","static int sbr_fail(const char *s){sbr_error(s);return SBR_ERROR;}","","int sbp_program(int budget)","{","    while(budget-->0){","        switch(sbp_pc){","        case 0:"]
        h += self._indent(["sbp_pc=1;break;"],3)
        for i,s in enumerate(self.p.statements):
            h += ["        case %d: /* BASIC line %d: %s */"%(self._pc(i),s.line,s.op)]
            h += self._indent(self.stmt(i,s),3)
        h += ["        default:return SBR_DONE;","        }","    }","    return SBR_RUNNING;","}",""]
        if self.errors:
            summary=", ".join("line %d %s"%x for x in self.errors[:16]);raise CompileError("native lowering not implemented: "+summary,self.errors[0][0])
        return "\n".join(h)


def emit_c(program: Program, name: str) -> str:
    return CEmitter(program,name).emit()


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input",type=Path);ap.add_argument("-o","--output",type=Path)
    ap.add_argument("--name",default="BASIC_PROGRAM");ap.add_argument("--dump-ir",action="store_true")
    ns=ap.parse_args(argv)
    try:
        program=compile_source(ns.input.read_text(encoding="latin1"),str(ns.input))
        text=json.dumps(program.to_dict(),indent=2,sort_keys=True)+"\n" if ns.dump_ir else emit_c(program,ns.name)
        if ns.output:ns.output.write_text(text,encoding="utf-8")
        else:sys.stdout.write(text)
        return 0
    except CompileError as e:
        print("%s:%d:%d: error: %s"%(ns.input,e.line,e.col,e.message),file=sys.stderr);return 1


if __name__ == "__main__": raise SystemExit(main())
