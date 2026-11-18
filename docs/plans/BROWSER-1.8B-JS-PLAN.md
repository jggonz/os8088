# Browser 1.8b - JavaScript 1.0-1.2 for the historical Web (1995-2000)

The owner's brief for 1.8b, after BROWSER-1.8-PLAN.md. The roadmap line is
in BROWSER-ROADMAP.md; 1.9 stays the general audit and optimisation.

## Where the tree stands (read before designing)

- **The browser has no JavaScript today.** `<script>` is dropped with its
  content (`br_tagtab`, TA_DROP), `javascript:` URLs are refused
  (brimage_part.asm), and no `on*` attribute is read. "Extend the existing
  implementation" therefore means: build on the browser's own structures
  (forms, links, the image table, frames, the one cookie, history,
  location) and on the WJS work in Weave, not on a script engine in the
  browser.
- **WJS (docs/WEAVE-SPEC.md section 4) is the nearest thing in the tree**,
  and what it lacks is most of this brief: 16-bit ints as the only number,
  no objects, no `new`, no `this`, no closures, no regex, no top-level
  statements, and it is compiled at pack time (host, or LOOM's overlay),
  never from source on the machine. What can be shared as SOURCE (the
  WEAVE-SPEC 1.2 rule): the tagged-cell value model, the handle table and
  its compacting collector (4.8), the event ring (4.9), the slice model
  (4.10) and the runaway-script stop (4.11). The language front end,
  objects, numbers and the browser object model are new.
- **Numbers**: there is no FPU and no `long` in the C toolchain. 1.0-1.2
  pages need decimals (`parseFloat`, a calculator, `Date`), so the number
  type must be a software one (32-bit integer plus a fixed or software
  floating form). Its cost is decided by measurement, not here.
- **It runs in a lazy part** (like the picture decoders), loaded only when
  a page has script and Options > JavaScript is on, so a page without
  script and a machine with JavaScript off pay nothing resident.

## The brief (owner's, verbatim)

Continue the existing JavaScript implementation, focusing on the most
commonly used scripts found on websites between 1995 and 2000.

JavaScript can be enabled or disabled through the browser's existing
Options menu. When disabled, scripts must not execute. Do not introduce
additional performance modes or artificial limitations for slower machines.

### 1. Most common JavaScript functionality

Prioritize compatibility with these historical use cases:

1. Form validation (`onsubmit`, `onchange`, `onfocus`, `onblur`).
2. Image rollovers (`onmouseover`, `onmouseout`, `document.images`).
3. Dialogs (`alert`, `confirm`, `prompt`).
4. Navigation and redirects (`location.href`, `location.replace`).
5. Popup windows (`window.open`, `window.close`).
6. Browser detection (`navigator.appName`, `navigator.appVersion`,
   `navigator.userAgent`).
7. Cookies (`document.cookie`).
8. Timers (`setTimeout`, `clearTimeout`, `setInterval`, `clearInterval`).
9. Dynamic dates, clocks and countdowns (`Date`).
10. Dropdown navigation (`select.options`, `selectedIndex`).
11. Image swapping and rotating banners.
12. Status bar messages (`window.status`).
13. Frame navigation (`parent`, `top`, `frames`).
14. History navigation (`history.back`, `history.forward`, `history.go`).
15. Dynamic HTML generation (`document.write`, `document.writeln`).
16. Basic calculators using form fields.
17. String and array manipulation.
18. Basic regular expressions (JavaScript 1.2).
19. Keyboard and mouse events.
20. Early DHTML functionality, including Netscape `document.layers` and
    Internet Explorer `document.all`, where practical.

Prioritize functionality over purely decorative effects.

### 2. Historical code examples

Use these as compatibility tests alongside existing tests.

**Form validation**
```html
<script>
function checkForm() {
  if (document.forms[0].name.value == "") {
    alert("Please enter your name");
    return false;
  }
  return true;
}
</script>

<form onsubmit="return checkForm()">
<input name="name">
<input type="submit">
</form>
```

**Image rollover**
```html
<a href="home.html"
 onmouseover="document.images['btn'].src='on.gif'"
 onmouseout="document.images['btn'].src='off.gif'">
<img name="btn" src="off.gif">
</a>
```

**Popup**
```javascript
window.open("help.html", "help",
            "width=400,height=300,scrollbars=yes");
```

**Clock and timer**
```javascript
function clock() {
  var now = new Date();
  window.status = now.toLocaleTimeString();
  setTimeout("clock()", 1000);
}
clock();
```

**Dropdown navigation**
```html
<select onchange="location.href=this.options[this.selectedIndex].value">
<option value="index.html">Home</option>
<option value="games.html">Games</option>
<option value="links.html">Links</option>
</select>
```

**Browser detection**
```javascript
if (navigator.appName == "Netscape") {
  alert("Netscape detected");
} else {
  alert("Another browser");
}
```

**Cookie**
```javascript
document.cookie = "visited=yes";
```

**Image rotation**
```javascript
var images = ["banner1.gif", "banner2.gif"];
var index = 0;

function rotate() {
  document.images["banner"].src = images[index];
  index = (index + 1) % images.length;
  setTimeout("rotate()", 3000);
}
```

**History**
```javascript
history.back();
history.forward();
```

**Dynamic content**
```javascript
document.write("<b>Welcome!</b>");
document.writeln("Current year: " +
                 new Date().getFullYear());
```

**Calculator**
```javascript
function calculate() {
  var a = parseFloat(document.forms[0].a.value);
  var b = parseFloat(document.forms[0].b.value);
  document.forms[0].result.value = a + b;
}
```

**JavaScript 1.2 syntax**
```javascript
var user = {name: "Guest", visits: 3};
var values = [10, 20, 30];

switch (user.visits) {
  case 3:
    alert("Welcome back!");
    break;
  default:
    alert("Hello!");
}

var pattern = /^[A-Za-z]+$/;
```

### 3. Implementation recommendations

- Extend the existing JavaScript implementation instead of replacing or
  duplicating it.
- Reuse existing browser structures for HTML elements, forms, links,
  images, frames, cookies and navigation.
- Expose lightweight JavaScript objects referencing existing browser
  structures whenever possible.
- Support inline `<script>` blocks, external scripts through `src`, and
  JavaScript event handler attributes.
- Support historical `javascript:` URLs where appropriate.
- Preserve JavaScript 1.0-1.2 syntax and behavior, including string-based
  timer callbacks.
- Handle differences between historical Netscape and Internet Explorer APIs
  when feasible.
- Avoid implementing modern DOM, ES6+, AJAX, or unrelated modern browser
  features.
- Ensure script errors do not crash the browser or prevent ordinary HTML
  rendering.
- Keep JavaScript execution conditional on the existing Options setting.
- Reuse the browser's existing memory management rather than introducing
  independent memory allocation mechanisms.
- Implement event dispatch and timers without blocking normal browser
  operation.
- Distinguish JavaScript language compatibility from browser API
  compatibility. Where a historical API cannot be supported, fail
  gracefully.

### 4. Validation

Test all examples above and add regression tests for:

- JavaScript enabled versus disabled.
- Multiple scripts executing in document order.
- Function declarations and calls.
- Global and local variables.
- Object property access and assignment.
- Array indexing and string operations.
- Event handlers returning `false` to cancel default actions.
- Timer creation, cancellation and repeated execution.
- Scripts interacting with forms and images.
- Invalid JavaScript syntax and runtime errors.
- HTML pages that contain unsupported JavaScript.
- Memory cleanup after navigating away from a page.

Use the actual browser implementation to determine which features are
already supported and which need work. Do not rewrite working components
unnecessarily.

**Goal:** Maximize practical compatibility with real JavaScript-powered
websites from 1995-2000, using JavaScript 1.0-1.2 as the primary language
target. Implement the most widely used functionality first, while
preserving the browser's existing architecture.

## Before the first line of code

1. **The survey** (agreed with the owner): Nintendo, Bored.com, Mad and
   other entertainment and fan sites, several years each (1997, 1999, 2001,
   2003), inner pages as well as front pages. Per page: which of the twenty
   features above it uses, and whether it works, degrades or breaks with
   script off. The table orders the work inside the brief's priorities.
2. **The design note**: the number type, the object model over the
   browser's structures, the part's size and the heap it takes, and how a
   `document.write` re-enters the parser - pinned in SPEC.md before code.
