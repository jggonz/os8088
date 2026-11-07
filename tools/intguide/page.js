(function(){
"use strict";
/* THE MODEL IS window.IG, written by tools/os88intguide.py. This script only
   draws it: nothing here knows a vector's name, a field's meaning or a
   step's words, and nothing here is specific to either chapter.

   STATE IS THREE THINGS. `rung` is the rail button that is lit, `step` the
   position inside it (-1 = the rung's own introduction), and `pick` a vector,
   field or region the reader clicked on a map - which overrides the step for
   the detail panel without moving the walk. Arrow keys move the walk and
   clear the pick, so the detail panel and the highlights always describe
   the same one thing.

   A STEP IS ONE OF FIVE THINGS: {vec}, {field}, {region}, {walk} (a frame of
   the rung's walk-through) or {note} (a paragraph of its own, which may
   light fields or vectors and set a map's colouring). */
var D = window.IG, R = D.rungs, V = D.vec, FL = D.fields;
var rung = 0, step = -1, pick = null;
var view = "kind", userView = false, fview = "kind", userFView = false;
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
var FPALE = {rsv:1, none:1, shared:1, out:1};
var PLANNED = {};
D.chapters.forEach(function(c){ if (c.planned) PLANNED[c.id] = 1; });
/* byte -> index of the field holding it, for 0400h..05FFh */
var FAT = {};
FL.forEach(function(f, i){ for (var b = f.off; b < f.off + f.len; b++) FAT[b] = i; });
function fidx(off){ return FAT[off]; }

/* ---------------------------------------------------------------- rail -- */
function railCol(r){
  if (!r.kind) return "";
  return r.chapter === "low" ? "var(--f-" + r.kind + ")" : kcol(r.kind);
}
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
      (r.kind ? "<i style='background:" + railCol(r) + "'></i>" : "") +
      "<span>" + r.title + "</span><span class='ms'>" + (r.ms || "") + "</span>";
    b.addEventListener("click", function(){ setRung(i, true); });
    host.insertBefore(b, nav);
  });
}

/* --------------------------------------------------------- low memory -- */
function buildLow(){
  var bar = $("lbar"), rule = $("lrule"), top = D.low.top;
  D.low.regions.forEach(function(g){
    var d = el("div", "rg");
    d.style.width = ((g.hi - g.lo) / top * 100) + "%";
    var c = "var(--c-" + g.col + ")";
    /* A chapter still to be written is drawn HATCHED: room, not contents. */
    d.style.background = PLANNED[g.chapter] ?
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

/* ------------------------------------------------------ the IVT grid -- */
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
        b.addEventListener("mouseenter", function(e){
          tipAt(e, "INT " + hx(n) + "h · 0000:" + hx(n * 4, 4), V[n].name); });
        b.addEventListener("mouseleave", untip);
      })(n);
      g.appendChild(b);
    }
  }
  viewButtons($("views"), D.views, function(id){ userView = true; setView(id); }, "vw-");
}
function viewButtons(host, list, fn, pre){
  host.appendChild(el("span", "lbl", "Colour by"));
  list.forEach(function(w){
    var b = el("button", "", w.name); b.type = "button"; b.id = pre + w.id;
    b.title = w.hint;
    b.addEventListener("click", function(){ fn(w.id); });
    host.appendChild(b);
  });
}
function tipAt(e, a, b){
  var t = $("ctip");
  t.innerHTML = "<span class='mono'>" + a + "</span>" + b;
  t.classList.add("on");
  var r = e.currentTarget.getBoundingClientRect();
  var w = t.offsetWidth;
  t.style.left = Math.min(Math.max(8, r.left + r.width / 2 - w / 2), window.innerWidth - w - 8) + "px";
  t.style.top = Math.max(8, r.top - t.offsetHeight - 6) + "px";
}
function untip(){ $("ctip").classList.remove("on"); }
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

/* -------------------------------------------- the byte map, 0400h..05FFh --
   Sixteen bytes a row, and a field is ONE element spanning its bytes, split
   only where it runs off the end of a row - so the keyboard ring reads as
   one thing across three rows rather than as thirty-two cells. */
function buildBytes(){
  var g = $("bmap");
  g.appendChild(el("div", "hd", ""));
  for (var c = 0; c < 16; c++) g.appendChild(el("div", "hd", "…" + hx(c, 1)));
  for (var row = 0x400; row < 0x600; row += 16){
    var rh = el("div", "rh", hx(row, 4));
    if (row === 0x500) rh.classList.add("brk");
    g.appendChild(rh);
    var b = row;
    while (b < row + 16){
      var i = fidx(b), f = FL[i], end = Math.min(f.off + f.len, row + 16);
      var s = el("button", "fseg" + (b !== f.off ? " cont" : ""));
      s.type = "button";
      s.style.gridColumn = "span " + (end - b);
      s.dataset.f = i;
      if (row === 0x500) s.classList.add("brk");
      s.innerHTML = "<span>" + (b === f.off ? (f.tag || "") : "") + "</span>";
      (function(i){
        s.addEventListener("click", function(){ pickField(FL[i].off); });
        s.addEventListener("mouseenter", function(e){
          var f = FL[i];
          tipAt(e, "0040:" + hx(f.off - 0x400, 4) + (f.len > 1 ? " · " + f.len + " bytes" : ""),
                f.name); });
        s.addEventListener("mouseleave", untip);
      })(i);
      g.appendChild(s);
      b = end;
    }
  }
  viewButtons($("fviews"), D.fviews, function(id){ userFView = true; setFView(id); }, "fv-");
}
function fcol(w, key){ return "var(--" + w.pre + "-" + key + ")"; }
function setFView(id){
  fview = id;
  var w = D.fviews.filter(function(x){ return x.id === id; })[0];
  D.fviews.forEach(function(x){
    $("fv-" + x.id).setAttribute("aria-pressed", x.id === id ? "true" : "false");
  });
  var segs = document.querySelectorAll("#bmap .fseg");
  for (var j = 0; j < segs.length; j++){
    var f = FL[+segs[j].dataset.f], key = f.own[id];
    segs[j].style.background = fcol(w, key);
    segs[j].classList.toggle("pale", !!FPALE[key]);
  }
  var lg = $("flegend"); lg.innerHTML = "";
  w.keys.forEach(function(k){
    var bytes = 0;
    FL.forEach(function(f){ if (f.own[id] === k) bytes += f.len; });
    if (!bytes) return;
    lg.appendChild(el("span", "", "<i style='background:" + fcol(w, k) + "'></i>" +
      w.names[k] + " <span class='mono' style='opacity:.7'>" + bytes + " B</span>"));
  });
  $("fcap").innerHTML = w.cap;
}

/* ----------------------------------------------------- the walk-throughs --
   A walk is a set of boxes and a list of frames; each frame says what every
   box holds (the generator carries unchanged boxes forward) and which boxes
   changed. Five kinds of box, each drawn by one function below. */
function boxBody(s){
  if (!s) return "";
  var h = "", i;
  if (s.k === "text"){
    h = "<div class='big'>" + s.big + "</div><div class='sm'>" + s.sm + "</div>";
    if (s.wire != null)
      h += "<div class='wire'><i style='width:" + (s.wire ? 100 : 0) + "%'></i></div>" +
           "<div class='sm'>" + s.wl + " " + (s.wire ? "<b>raised</b>" : "low") + "</div>";
  } else if (s.k === "bits"){
    h = "<div class='bits'><span class='k'></span>";
    for (i = 7; i >= 0; i--) h += "<span class='h'>" + i + "</span>";
    s.rows.forEach(function(r){
      h += "<span class='k'>" + r[0] + "</span>";
      for (var b = 7; b >= 0; b--){
        var on = (r[1] >> b) & 1;
        h += "<span class='b" + (on ? " on" : "") + "'" +
             (s.hi === b ? " style='outline:1px solid var(--ink)'" : "") + ">" + on + "</span>";
      }
    });
    h += "</div>";
    if (s.wire != null)
      h += "<div class='wire'><i style='width:" + (s.wire ? 100 : 0) + "%'></i></div>" +
           "<div class='sm'>" + s.wl + " " + (s.wire ? "<b>raised</b>" : "quiet") + "</div>";
  } else if (s.k === "kv"){
    h = "<div class='regs'>";
    s.rows.forEach(function(r){
      h += "<span class='k'>" + r[0] + "</span><span" + (r[2] ? " class='chg'" : "") +
           ">" + r[1] + "</span>";
    });
    h += "</div>" + (s.note ? "<div class='sm'>" + s.note + "</div>" : "");
  } else if (s.k === "list"){
    h = "<div class='stk'>";
    s.rows.forEach(function(r){
      h += "<div class='" + (r[3] || "") + "'><span class='a'>" + r[0] + "</span><span>" +
           r[1] + "</span><span class='a'>" + r[2] + "</span></div>";
    });
    h += "</div>";
  } else if (s.k === "code"){
    h = "<div class='code'>";
    s.rows.forEach(function(r, j){
      h += "<div class='" + (s.on === j ? "on" : (r[1] ? "c" : "")) + "'>" + r[0] + "</div>";
    });
    h += "</div>";
  }
  return h;
}
function drawWalk(key, i){
  var W = D.walks[key], w = W.steps[i], M = $("mech");
  $("mechT").textContent = W.title; $("mechH").innerHTML = W.hint;
  M.innerHTML = "";
  W.boxes.forEach(function(bx){
    var b = el("div", "box" + (w.on.indexOf(bx.id) >= 0 ? " on" : ""));
    b.innerHTML = "<h4><span>" + bx.title + "</span><span>" + (bx.right || "") +
                  "</span></h4>" + boxBody(w.b[bx.id]);
    M.appendChild(b);
  });
  var f = $("flow"); f.innerHTML = "";
  W.steps.forEach(function(x, j){
    var b = el("button", j === i ? "on" : (j < i ? "done" : ""), (j + 1) + " " + x.short);
    b.type = "button";
    b.addEventListener("click", function(){ step = j; pick = null; draw(); });
    f.appendChild(b);
  });
}

/* ------------------------------------------------------------- detail -- */
function factRows(rows){
  return (rows || []).map(function(r){
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
function fieldDetail(i){
  var f = FL[i];
  var lin = hx(f.off, 4) + (f.len > 1 ? "–" + hx(f.off + f.len - 1, 4) : "");
  $("dtitle").innerHTML = f.name + " <span style='color:var(--dim);font-weight:400'>· " +
                          lin + "h</span>";
  var h = f.html;
  if (f.os)  h += "<h5><i style='background:var(--u-write)'></i>On os8088</h5>" + f.os;
  if (f.dos) h += "<h5><i style='background:var(--o-dos)'></i>For a DOS program on os8088</h5>" + f.dos;
  $("dbody").innerHTML = h;
  var rows = [
    ["Address", "0040:" + hx(f.off - 0x400, 4)],
    ["Linear", lin + "h"],
    ["Size", f.len + (f.len === 1 ? " byte" : " bytes")]
  ];
  D.fviews.forEach(function(w){
    rows.push([w.fact || "Group", w.names[f.own[w.id]], 1]);
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
  $("dfacts").innerHTML = factRows(facts);
}

/* ------------------------------------------------------- the one draw -- */
function cur(){
  var r = R[rung];
  if (pick) return pick;
  if (step >= 0 && r.steps.length) return r.steps[step];
  return null;
}
function draw(){
  var r = R[rung], c = cur(), low = r.chapter === "low";
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
  $("ivtp").hidden = low; $("bytp").hidden = !low;

  /* the vector grid: the rung's (or the note's) set, then the one picked */
  var vset = {}, vl = (c && c.note && c.vecs) || r.vecs || [];
  vl.forEach(function(n){ vset[n] = 1; });
  var hot = c && c.vec != null ? c.vec : (r.walk ? D.walks[r.walk].vec : -1);
  for (var n = 0; n < 256; n++){
    var b = $("v" + n);
    b.classList.toggle("dim", vl.length > 0 && !vset[n] && n !== hot);
    b.classList.toggle("hot", n === hot);
  }
  /* the byte map, the same way */
  var fset = {}, fl = (c && c.note && c.fields) || r.fields || [];
  fl.forEach(function(o){ fset[fidx(o)] = 1; });
  var fhot = c && c.field != null ? fidx(c.field) : -1;
  var segs = document.querySelectorAll("#bmap .fseg");
  for (var j = 0; j < segs.length; j++){
    var fi = +segs[j].dataset.f;
    segs[j].classList.toggle("dim", fl.length > 0 && !fset[fi] && fi !== fhot);
    segs[j].classList.toggle("hot", fi === fhot);
  }
  var hotRg = c && c.region ? c.region : null;
  if (!hotRg && c && c.field != null) hotRg = c.field >= 0x500 ? "low" : "bda";
  D.low.regions.forEach(function(g){
    var d = $("rg-" + g.id), on = hotRg || r.region || (low && fl.length ?
      (fl[0] >= 0x500 ? "low" : "bda") : null);
    d.classList.toggle("hot", g.id === on);
    d.classList.toggle("dim", !!on && g.id !== on);
  });
  if (!userView) setView((c && c.note && c.view) || r.view || "kind");
  if (!userFView) setFView((c && c.note && c.fview) || r.fview || "kind");

  var mp = $("mechp");
  if (r.walk){ mp.hidden = false; drawWalk(r.walk, step < 0 ? 0 : step); }
  else mp.hidden = true;

  if (c && c.vec != null) vecDetail(c.vec);
  else if (c && c.field != null) fieldDetail(fidx(c.field));
  else if (c && c.region) regionDetail(c.region);
  else if (c && c.note) proseDetail(c.t, c.h, c.facts);
  else if (r.walk && step >= 0){
    var w = D.walks[r.walk].steps[step];
    proseDetail((step + 1) + ". " + w.t, w.h, w.facts);
  }
  else proseDetail(r.title, r.intro, r.facts);

  $("prev").disabled = rung === 0 && step < 0;
  $("next").disabled = rung === R.length - 1 && step === R[rung].steps.length - 1;
}
function enter(i, s){
  rung = i; step = s; pick = null; userView = false; userFView = false;
}
function setRung(i, fromClick){
  if (i < 0 || i >= R.length) return;
  if (fromClick && i === rung && pick == null && step >= 0){ step = -1; draw(); return; }
  enter(i, -1);
  draw();
}
function seek(d){
  pick = null;
  var r = R[rung];
  if (d > 0){
    if (step < r.steps.length - 1){ step++; }
    else if (rung < R.length - 1){ enter(rung + 1, -1); }
  } else {
    if (step > -1){ step--; }
    else if (rung > 0){ enter(rung - 1, R[rung - 1].steps.length - 1); }
  }
  draw();
}
/* A click on a map JUMPS to that thing's place in the walk when it has one
   in this rung, and otherwise shows it without leaving the rung - so a
   reader exploring a map never loses the thread they were on. */
function pickIn(key, val){
  var r = R[rung];
  for (var j = 0; j < r.steps.length; j++)
    if (r.steps[j][key] === val){ step = j; pick = null; draw(); return true; }
  return false;
}
function pickVec(n){
  if (pickIn("vec", n)) return;
  pick = {vec: n}; draw(); reveal();
}
function pickField(off){
  off = FL[fidx(off)].off;
  if (pickIn("field", off)) return;
  pick = {field: off}; draw(); reveal();
}
function pickRegion(id){
  if (pickIn("region", id)) return;
  pick = {region: id}; draw(); reveal();
}
/* On a narrow screen the detail sits ABOVE the maps, so a click low in one
   would change text the reader cannot see. Bring it into view. */
function reveal(){
  var d = $("dpanel"), r = d.getBoundingClientRect();
  if (r.top < 0 || r.top > window.innerHeight - 80)
    d.scrollIntoView({behavior: "smooth", block: "start"});
}
function jumpHome(key, val){
  for (var i = 0; i < R.length; i++)
    for (var j = 0; j < R[i].steps.length; j++)
      if (R[i].steps[j][key] === val && R[i].home){ enter(i, j); draw(); return true; }
  return false;
}
window.igJump = function(n){ if (!jumpHome("vec", n)) pickVec(n); };
window.igField = function(off){
  off = FL[fidx(off)].off;
  if (!jumpHome("field", off)) pickField(off);
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

buildChapters(); buildRail(); buildLow(); buildGrid(); buildBytes();
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
/* #int09 opens a vector, #f0417 a field, #<rung> a rung. */
function route(){
  var h0 = (location.hash || "").replace("#", "");
  if (/^int[0-9a-f]{2}$/i.test(h0)) window.igJump(parseInt(h0.slice(3), 16));
  else if (/^f[0-9a-f]{4}$/i.test(h0)) window.igField(parseInt(h0.slice(1), 16));
  else if (h0) window.igRung(h0);
}
window.addEventListener("hashchange", route);
route();
draw();
})();
