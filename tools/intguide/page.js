(function(){
"use strict";
/* THE MODEL IS window.IG, written by tools/os88intguide.py. This script only
   draws it: nothing here knows a vector's name, a colour's meaning or a
   step's words.

   STATE IS THREE THINGS. `rung` is the rail button that is lit, `step` the
   position inside it (-1 = the rung's own introduction), and `pick` a vector
   or a region the reader clicked on the map - which overrides the step for
   the detail panel without moving the walk. Arrow keys move the walk and
   clear the pick, so the detail panel and the highlights always describe
   the same one thing. */
var D = window.IG, R = D.rungs, V = D.vec;
var rung = 0, step = -1, pick = null, view = "kind", userView = false;
var $ = function(id){ return document.getElementById(id); };
function el(tag, cls, html){
  var e = document.createElement(tag);
  if (cls) e.className = cls;
  if (html != null) e.innerHTML = html;
  return e;
}
function hx(n, w){ var s = n.toString(16).toUpperCase(); while (s.length < (w||2)) s = "0" + s; return s; }
function kcol(k){ return "var(--k-" + k + ")"; }
function ocol(o){ return "var(--o-" + o + ")"; }
var PALE = {free:1, none:1, basic:1};

/* ---------------------------------------------------------------- rail -- */
function buildRail(){
  var host = $("stages"), nav = $("navbtns");
  var lastGrp = null;
  R.forEach(function(r, i){
    if (r.grp !== lastGrp){
      var g = el("div", "grp"); g.innerHTML = "<span>" + r.grp + "</span>";
      host.insertBefore(g, nav); lastGrp = r.grp;
    }
    var b = el("button", "st");
    b.type = "button"; b.id = "st" + i;
    b.innerHTML = "<span class='n'>" + (i + 1) + "</span>" +
      (r.kind ? "<i style='background:" + kcol(r.kind) + "'></i>" : "") +
      "<span>" + r.title + "</span><span class='ms'>" + (r.ms || "") + "</span>";
    b.addEventListener("click", function(){ setRung(i, true); });
    host.insertBefore(b, nav);
  });
}

/* --------------------------------------------------------- low memory -- */
function buildLow(){
  var bar = $("lbar"), rule = $("lrule"), top = D.low.top;
  D.low.regions.forEach(function(g){
    var d = el("div", "rg" + (g.chapter !== "int" ? " next" : ""));
    d.style.width = ((g.hi - g.lo) / top * 100) + "%";
    var c = "var(--c-" + g.col + ")";
    /* A chapter still to be written is drawn HATCHED: room, not contents. */
    d.style.background = g.chapter !== "int" ?
      "repeating-linear-gradient(135deg," + c + " 0 7px,color-mix(in srgb," + c +
      " 70%,var(--panel)) 7px 13px)" : c;
    d.id = "rg-" + g.id;
    d.innerHTML = g.name + "<small>" + hx(g.lo, 4) + "–" + hx(g.hi - 1, 4) +
                  " · " + (g.hi - g.lo).toLocaleString() + " bytes</small>";
    d.title = g.name;
    d.addEventListener("click", function(){ pickRegion(g.id); });
    bar.appendChild(d);
  });
  D.low.ticks.forEach(function(t, i, a){
    var s = el("span", i === 0 ? "e0" : (i === a.length - 1 ? "e1" : ""),
               t[1] || (hx(t[0], 4) + "h"));
    s.style.left = (t[0] / top * 100) + "%";
    rule.appendChild(s);
  });
}

/* --------------------------------------------------------------- grid -- */
function buildGrid(){
  var g = $("grid");
  g.appendChild(el("div", "hd", ""));
  for (var c = 0; c < 16; c++) g.appendChild(el("div", "hd", "…" + hx(c, 1)));
  for (var r = 0; r < 16; r++){
    g.appendChild(el("div", "rh", hx(r, 1) + "…"));
    for (var c2 = 0; c2 < 16; c2++){
      var n = r * 16 + c2, v = V[n];
      var b = el("button", "cell");
      b.type = "button"; b.id = "v" + n;
      b.setAttribute("aria-label", "INT " + hx(n) + "h " + v.name);
      b.innerHTML = "<span class='x'>" + hx(n) + "</span><span class='t'>" +
                    (v.tag || "") + "</span>";
      (function(n){
        b.addEventListener("click", function(){ pickVec(n); });
        b.addEventListener("mouseenter", function(e){ ctip(e, n); });
        b.addEventListener("mouseleave", function(){ $("ctip").classList.remove("on"); });
      })(n);
      g.appendChild(b);
    }
  }
  var vh = $("views");
  vh.appendChild(el("span", "lbl", "Colour by"));
  D.views.forEach(function(w){
    var b = el("button", "", w.name); b.type = "button"; b.id = "vw-" + w.id;
    b.title = w.hint;
    b.addEventListener("click", function(){ userView = true; setView(w.id); });
    vh.appendChild(b);
  });
}
function ctip(e, n){
  var t = $("ctip"), v = V[n];
  t.innerHTML = "<span class='mono'>INT " + hx(n) + "h · 0000:" + hx(n * 4, 4) +
                "</span>" + v.name;
  t.classList.add("on");
  var r = e.currentTarget.getBoundingClientRect();
  var w = t.offsetWidth;
  t.style.left = Math.min(Math.max(8, r.left + r.width / 2 - w / 2), window.innerWidth - w - 8) + "px";
  t.style.top = Math.max(8, r.top - t.offsetHeight - 6) + "px";
}
function setView(id){
  view = id;
  D.views.forEach(function(w){
    $("vw-" + w.id).setAttribute("aria-pressed", w.id === id ? "true" : "false");
  });
  var w = D.views.filter(function(x){ return x.id === id; })[0];
  for (var n = 0; n < 256; n++){
    var v = V[n], b = $("v" + n), key, c;
    if (id === "kind"){ key = v.kind; c = kcol(key); }
    else { key = v.own[id]; c = ocol(key); }
    b.style.background = c;
    b.classList.toggle("pale", !!PALE[key]);
    b.classList.toggle("mark", id !== "kind" && !!(v.mark && v.mark[id]));
  }
  var lg = $("legend"); lg.innerHTML = "";
  var keys = id === "kind" ? D.kindOrder : D.ownOrder;
  var pal  = id === "kind" ? D.kinds : D.owners;
  keys.forEach(function(k){
    var counts = 0;
    for (var n = 0; n < 256; n++)
      if ((id === "kind" ? V[n].kind : V[n].own[id]) === k) counts++;
    if (!counts) return;
    lg.appendChild(el("span", "", "<i style='background:" +
      (id === "kind" ? kcol(k) : ocol(k)) + "'></i>" + pal[k] +
      " <span class='mono' style='opacity:.7'>" + counts + "</span>"));
  });
  if (id !== "kind")
    lg.appendChild(el("span", "", "<span class='dot'></span>" + w.mark));
  $("gcap").innerHTML = w.cap;
}

/* ----------------------------------------------------- the walk-through -- */
function bitsRow(lbl, v, hi){
  var s = "<span class='k'>" + lbl + "</span>";
  for (var b = 7; b >= 0; b--){
    var on = (v >> b) & 1;
    s += "<span class='b" + (on ? " on" : "") + "'" +
         (hi === b ? " style='outline:1px solid var(--ink)'" : "") + ">" + on + "</span>";
  }
  return s;
}
function drawWalk(i){
  var w = D.walk[i], M = $("mech");
  M.innerHTML = "";
  function box(id, title, right, body){
    var b = el("div", "box" + (w.on.indexOf(id) >= 0 ? " on" : "") +
                      (w.off && w.off.indexOf(id) >= 0 ? " off" : ""));
    b.innerHTML = "<h4><span>" + title + "</span><span>" + (right || "") + "</span></h4>" + body;
    M.appendChild(b);
  }
  box("dev", D.walkDev.name, D.walkDev.port,
      "<div class='big'>" + w.dev.big + "</div><div class='sm'>" + w.dev.sm + "</div>" +
      "<div class='wire' title='the interrupt request line'><i style='width:" +
      (w.dev.line ? 100 : 0) + "%'></i></div><div class='sm'>request line " +
      (w.dev.line ? "<b>raised</b>" : "low") + "</div>");
  var p = w.pic, hl = D.walkDev.irq;
  var hdr = "<span class='k'></span>";
  for (var b = 7; b >= 0; b--) hdr += "<span class='h'>" + b + "</span>";
  box("pic", "Interrupt controller", "8259A",
      "<div class='bits'>" + hdr + bitsRow("IRR", p.irr, hl) + bitsRow("IMR", p.imr, hl) +
      bitsRow("ISR", p.isr, hl) + "</div><div class='wire'><i style='width:" +
      (p.intr ? 100 : 0) + "%'></i></div><div class='sm'>to the CPU: " +
      (p.intr ? "<b>INTR raised</b>" : "quiet") + "</div>");
  var c = w.cpu, ch = c.chg || [];
  function reg(k, v){ return "<span class='k'>" + k + "</span><span" +
    (ch.indexOf(k) >= 0 ? " class='chg'" : "") + ">" + v + "</span>"; }
  box("cpu", "Processor", "8088",
      "<div class='regs'>" + reg("CS:IP", c.cs + ":" + c.ip) + reg("SS:SP", c.ss + ":" + c.sp) +
      reg("FLAGS", c.fl) + reg("IF", c.iff) + "</div><div class='sm'>" + c.note + "</div>");
  var s = "<div class='stk'>";
  w.stk.forEach(function(r){
    s += "<div class='" + (r[3] ? "new" : "") + (r[4] ? " sp" : "") + "'><span class='a'>" +
         r[0] + "</span><span>" + r[1] + "</span><span class='a'>" + r[2] + "</span></div>";
  });
  box("stk", "Stack", "grows down", s + "</div>");
  var t = "<div class='ivt1'>";
  D.walkIvt.forEach(function(r, j){
    t += "<div class='" + (w.ivt && r[3] ? "on" : "") + "'><span>" + r[0] +
         "</span><span>" + r[1] + "</span><span style='opacity:.75'>" + r[2] + "</span></div>";
  });
  box("ivt", "Interrupt table", "0000:0020", t + "</div>");
  var k = "<div class='code'>";
  D.walkCode.forEach(function(r, j){
    k += "<div class='" + (w.code === j ? "on" : (r[1] ? "c" : "")) + "'>" + r[0] + "</div>";
  });
  box("isr", "Handler", D.walkDev.isr, k + "</div>");
  var f = $("flow"); f.innerHTML = "";
  D.walk.forEach(function(x, j){
    var b = el("button", j === i ? "on" : (j < i ? "done" : ""), (j + 1) + " " + x.short);
    b.type = "button";
    b.addEventListener("click", function(){ step = j; pick = null; draw(); });
    f.appendChild(b);
  });
}

/* ------------------------------------------------------------- detail -- */
function factRows(rows){
  return rows.map(function(r){
    return "<div><span class='k'>" + r[0] + "</span><span class='v" + (r[2] ? " w" : "") +
           "'>" + r[1] + "</span></div>";
  }).join("");
}
function vecDetail(n){
  var v = V[n];
  $("dtitle").innerHTML = "INT " + hx(n) + "h <span style='color:var(--dim);font-weight:400'>· " +
                          v.name + "</span>";
  var h = v.html || "";
  if (v.os)  h += "<h5><i style='background:var(--o-os)'></i>On os8088</h5>" + v.os;
  if (v.dos) h += "<h5><i style='background:var(--o-dos)'></i>For a DOS program on os8088</h5>" + v.dos;
  $("dbody").innerHTML = h;
  var rows = [
    ["Number", hx(n) + "h · " + n],
    ["Table entry", "0000:" + hx(n * 4, 4) + "–" + hx(n * 4 + 3, 4)],
    ["Kind", D.kinds[v.kind], 1],
    ["Arrives by", v.by, 1]
  ];
  if (v.irq != null) rows.push(["Hardware line", "IRQ " + v.irq]);
  D.views.forEach(function(w){
    if (w.id !== "kind") rows.push([w.fact, D.owners[v.own[w.id]], 1]);
  });
  $("dfacts").innerHTML = factRows(rows);
}
function regionDetail(id){
  var g = D.low.regions.filter(function(x){ return x.id === id; })[0];
  $("dtitle").innerHTML = g.name + " <span style='color:var(--dim);font-weight:400'>· " +
                          hx(g.lo, 4) + "h–" + hx(g.hi - 1, 4) + "h</span>";
  $("dbody").innerHTML = g.html;
  $("dfacts").innerHTML = factRows(g.facts);
}
function proseDetail(title, html, facts){
  $("dtitle").innerHTML = title;
  $("dbody").innerHTML = html;
  $("dfacts").innerHTML = facts ? factRows(facts) : "";
}

/* ------------------------------------------------------- the one draw -- */
function cur(){
  var r = R[rung];
  if (pick) return pick;
  if (step >= 0 && r.steps.length) return r.steps[step];
  return null;
}
function draw(){
  var r = R[rung], c = cur();
  R.forEach(function(x, i){
    $("st" + i).setAttribute("aria-current", i === rung ? "true" : "false");
  });
  D.chapters.forEach(function(ch){
    $("ch-" + ch.id).setAttribute("aria-pressed", ch.id === r.chapter ? "true" : "false");
  });
  $("stitle").innerHTML = r.title;
  var where = r.steps.length ?
    (step < 0 ? "introduction · " + r.steps.length + " to walk" :
     (step + 1) + " of " + r.steps.length) : "";
  $("smovedt").innerHTML = r.sub + (where ? " · <b>" + where + "</b>" : "");
  $("where").textContent = "rung " + (rung + 1) + " of " + R.length +
                           (r.steps.length && step >= 0 ? " · step " + (step + 1) : "");

  /* the map's highlights: the rung's own set, then the one picked */
  var set = {};
  (r.vecs || []).forEach(function(n){ set[n] = 1; });
  var hot = c && c.vec != null ? c.vec : (r.walk ? D.walkDev.vec : -1);
  for (var n = 0; n < 256; n++){
    var b = $("v" + n);
    b.classList.toggle("dim", r.vecs && r.vecs.length > 0 && !set[n] && n !== hot);
    b.classList.toggle("hot", n === hot);
  }
  var hotRg = c && c.region ? c.region : null;
  D.low.regions.forEach(function(g){
    var d = $("rg-" + g.id);
    d.classList.toggle("hot", g.id === hotRg || (!hotRg && r.region === g.id));
    d.classList.toggle("dim", !!(r.region || hotRg) && g.id !== (hotRg || r.region));
  });
  if (!userView) setView(r.view || "kind");

  var mp = $("mechp");
  if (r.walk){
    mp.hidden = false;
    drawWalk(step < 0 ? 0 : step);
  } else mp.hidden = true;

  if (c && c.vec != null) vecDetail(c.vec);
  else if (c && c.region) regionDetail(c.region);
  else if (r.walk && step >= 0) proseDetail((step + 1) + ". " + D.walk[step].t, D.walk[step].h,
                                            D.walk[step].facts);
  else proseDetail(r.title, r.intro, r.facts);

  $("prev").disabled = rung === 0 && step < 0;
  $("next").disabled = rung === R.length - 1 && step === R[rung].steps.length - 1;
}
function setRung(i, fromClick){
  if (i < 0 || i >= R.length) return;
  if (fromClick && i === rung && pick == null && step >= 0){ step = -1; draw(); return; }
  rung = i; step = -1; pick = null; userView = false;
  draw();
}
function seek(d){
  pick = null;
  var r = R[rung];
  if (d > 0){
    if (step < r.steps.length - 1){ step++; }
    else if (rung < R.length - 1){ rung++; step = -1; userView = false; }
  } else {
    if (step > -1){ step--; }
    else if (rung > 0){ rung--; step = R[rung].steps.length - 1; userView = false; }
  }
  draw();
}
/* A click on the map JUMPS to that vector's place in the walk when it has
   one in this rung, and otherwise shows it without leaving the rung - so a
   reader exploring the table never loses the thread they were on. */
function pickVec(n){
  var r = R[rung];
  for (var j = 0; j < r.steps.length; j++)
    if (r.steps[j].vec === n){ step = j; pick = null; draw(); return; }
  pick = {vec: n}; draw(); reveal();
}
/* On a narrow screen the detail sits ABOVE the table, so a click low in the
   grid would change text the reader cannot see. Bring it into view. */
function reveal(){
  var d = $("dpanel"), r = d.getBoundingClientRect();
  if (r.top < 0 || r.top > window.innerHeight - 80)
    d.scrollIntoView({behavior: "smooth", block: "start"});
}
function pickRegion(id){
  var r = R[rung];
  for (var j = 0; j < r.steps.length; j++)
    if (r.steps[j].region === id){ step = j; pick = null; draw(); return; }
  pick = {region: id}; draw(); reveal();
}
window.igJump = function(n){
  for (var i = 0; i < R.length; i++)
    for (var j = 0; j < R[i].steps.length; j++)
      if (R[i].steps[j].vec === n && R[i].home){ rung = i; step = j; pick = null; userView = false; draw(); return; }
  pickVec(n);
};
window.igRung = function(id){
  for (var i = 0; i < R.length; i++) if (R[i].id === id){ setRung(i); return; }
};

function buildChapters(){
  var h = $("chap");
  D.chapters.forEach(function(ch){
    var b = el("button", ch.planned ? "planned" : "", "<b>" + ch.n + "</b>" + ch.name);
    b.type = "button"; b.id = "ch-" + ch.id;
    b.title = ch.hint;
    b.addEventListener("click", function(){
      for (var i = 0; i < R.length; i++) if (R[i].chapter === ch.id){ setRung(i); return; }
    });
    h.appendChild(b);
  });
}

buildChapters(); buildRail(); buildLow(); buildGrid();
$("prev").addEventListener("click", function(){ seek(-1); });
$("next").addEventListener("click", function(){ seek(1); });
document.addEventListener("keydown", function(e){
  if (e.target && /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) return;
  if (e.altKey || e.ctrlKey || e.metaKey) return;
  if (e.key === "ArrowRight"){ seek(1); e.preventDefault(); }
  else if (e.key === "ArrowLeft"){ seek(-1); e.preventDefault(); }
  else if (e.key === "ArrowDown"){ setRung(rung + 1); e.preventDefault(); }
  else if (e.key === "ArrowUp"){ setRung(rung - 1); e.preventDefault(); }
  else if (e.key === "Escape"){ pick = null; draw(); }
  else if (e.key === "Home"){ setRung(0); e.preventDefault(); }
  else if (e.key === "End"){ setRung(R.length - 1); e.preventDefault(); }
});
/* One shared popup for the glossary, positioned rather than CSS-only so the
   panel it sits in cannot clip it - the ladder's, unchanged. */
(function(){
  var tip = el("div"); tip.id = "gtip"; document.body.appendChild(tip);
  var ct = el("div"); ct.id = "ctip"; document.body.appendChild(ct);
  function show(e){
    var k = e.target && e.target.dataset && e.target.dataset.g;
    if (!k || !D.gloss[k]) return;
    tip.innerHTML = "";
    var h = document.createElement("b"); h.textContent = k;
    tip.appendChild(h);
    tip.appendChild(document.createTextNode(D.gloss[k]));
    tip.classList.add("on");
    var r = e.target.getBoundingClientRect();
    tip.style.left = "0px"; tip.style.top = "0px";
    var w = tip.offsetWidth, hh = tip.offsetHeight;
    var x = Math.min(Math.max(8, r.left + r.width / 2 - w / 2), window.innerWidth - w - 8);
    var y = r.top - hh - 8;
    if (y < 8) y = r.bottom + 8;
    tip.style.left = x + "px"; tip.style.top = y + "px";
  }
  function hide(){ tip.classList.remove("on"); }
  function isGl(e){ return e.target.classList && e.target.classList.contains("gl"); }
  document.addEventListener("mouseover", function(e){ if (isGl(e)) show(e); });
  document.addEventListener("mouseout", function(e){ if (isGl(e)) hide(); });
  document.addEventListener("focusin", function(e){ if (isGl(e)) show(e); });
  document.addEventListener("focusout", hide);
  window.addEventListener("scroll", function(){ hide(); ct.classList.remove("on"); }, true);
})();
var h0 = (location.hash || "").replace("#", "");
if (/^int[0-9a-f]{2}$/i.test(h0)) window.igJump(parseInt(h0.slice(3), 16));
else if (h0) window.igRung(h0);
draw();
})();
