# Weave

Weave runs Weave apps. A Weave app is a small program made the way a web
page is made: a layout of buttons, fields, labels and other parts, a script
that says what happens when you use them, and, for a spreadsheet, a set of
formulas. All of it is packed into one file whose name ends in `.WAB`, and
Weave is what opens that file and runs it in a window.

Three apps come with it: a greeting form, a picnic budget spreadsheet and a
game of Pong. You can change any of them, or make your own, with Loom ([[LOOM.O88]]).

![The Greeter app, after greeting Ada three times with Shout it ticked.](shot:weave-form){full}

## Starting Weave

Weave is on the Weave disk, in the `WEAVE` folder, beside the three demo
apps [[FORM.WAB]], [[SHEET.WAB]] and [[PONG.WAB]]. Double-click any `.WAB` file
and Weave opens with that app running. A `.WAB` file opens in Weave from any
folder, as long as `WEAVE.O88` is still on the disk.

Every app you open gets a Weave window of its own, so you can run several at
once. Each one needs memory: a 640KB machine holds four or five small apps
at a time, or two and Loom. A 256KB machine runs one app at a time, and a
second double-click is refused with **Out of memory**. If an app needs more
memory than is free, Weave says so in its window, for example **This app
needs 42KB; the largest free run is 30KB**.

If you double-click `WEAVE.O88` itself, the window opens with no app in it
and says how to open one.

![Weave started on its own, with no app open.](shot:weave-empty){full}

## Using an app

A Weave app works like any other window. Click a button to press it, click
a check box to tick it, and click a field to type in it. The window can be
moved and resized, and the app lays itself out again to fit.

| Key | What it does |
|---|---|
| `Tab` | Move to the next field you can type in |
| `Enter` | Finish typing in a field. Many apps act on it, as if you had pressed their button |
| Arrow keys | Move around a spreadsheet grid |
| `Esc` | Cancel what you typed into a spreadsheet's formula bar |
| `Ctrl-R` | Reload the app from its file and start it again |

An app can add up to four menus of its own to the menu bar, after Weave's
own **Bundle** menu. Some apps open small message boxes; click **OK** to
close them.

## The Greeter (FORM.WAB)

The Greeter is the smallest demo: a form with a name field, a button, a
check box and a meter.

1. Click the **Your name** field, or press `Tab`, and type a name.
2. Click **Greet**, or press `Enter`. The line under the check box says
   hello to you.
3. Tick **Shout it** and greet again: now it shouts, in capitals.

The meter under **Greetings so far** fills a little more with each
greeting, up to ten. **Reset**, or **Party > Reset the tally** in the menu
bar, empties it and shows a message box saying the tally was reset. If you
press **Greet** with the field empty, a message box asks you to type a name
first.

## The Picnic Budget (SHEET.WAB)

The Picnic Budget is a small spreadsheet. It lists three items and their
costs, and works out the total, the total rounded to a whole number, the
average, and whether the picnic comes in under 25.

![The budget after changing Bread to 4.25: the total and the average follow.](shot:weave-sheet){right}

Click a cell to select it. The line under the grid shows the cell's
position and value, such as `R2C2 = 3`. That line shows whole numbers only;
the grid itself shows the decimals. Use the arrow keys to move the
selection; the grid scrolls when you go past its edge, so press the down
arrow past row 7 to see row 8, **Cheap?**, which shows 1 for yes and 0 for
no.

To change a cell:

1. Click the cell, then click the long bar above the grid (the formula bar).
2. Type a number, some words, or a formula starting with `=`, for example
   `=B2*2`.
3. Press `Enter` to put it in the cell, or `Esc` to leave the cell as it
   was.

Every formula in the sheet is worked out again straight away, and the line
under the grid says how many cells changed. Formulas can use `+ - * /`,
comparisons such as `<` and `>=`, cell names like `B4`, ranges like
`B2:B4`, and the functions `SUM`, `MIN`, `MAX`, `AVG`, `COUNT`, `IF`, `ABS`
and `ROUND`. A division by zero shows `#DIV0`.

![The Show total button's message.](shot:weave-alert){left}

A formula that came with the app shows as `=?` in the formula bar, because
the app carries the worked-out formula and not its text. Typing over it
replaces it.

**Show total** opens a message with the total as a whole number.
**Cider +1** adds one to the cost of the cider.

## Pong (PONG.WAB)

Pong is the game, for one player against the computer. You are the cyan
paddle on the left; the computer plays the magenta one on the right.

| Key or button | What it does |
|---|---|
| **Serve** | Put the ball in play. Serves alternate between the two ends |
| `A` | Move your paddle up |
| `Z` | Move your paddle down |

![A rally in Weave Pong.](shot:weave-pong){right}

The ball bounces off the top and bottom. Where it hits a paddle decides how
steeply it comes back: the middle sends it back flat, an edge sends it away
at a sharp angle, and every return is a little faster. The computer's paddle
moves slowly, so a steep return can beat it. When the ball goes out at
either side, the point goes to the other player, the score at the top
changes and play stops until you press **Serve** again. The speaker beeps
high on each hit and low on each point.

## The Bundle menu

| Item | Key | What it does |
|---|---|---|
| Open Bundle... | | Choose a `.WAB` file to run in this window |
| Reload | `Ctrl-R` | Read the app's file again and start it from the beginning |
| Bundle Info | | Show technical details of the app |

![The Bundle menu.](shot:weave-menu){right}

**Reload** is for when you have just changed an app in Loom and packed it
again: click the Weave window and press `Ctrl-R` to see the new version
straight away.

**Bundle Info** lists, over the app, how much memory it asked for and one
line for each of its parts with its position and size. If the app has
reported an error, the whole message is on the last line. The app comes back
the next time its window is redrawn, or when you reload it.

The **Weave** menu holds **About Weave** and **Close**. Reload and Bundle
Info are grey until an app is open.

## When something goes wrong

- If a file is not a Weave app, or is damaged, Weave says so in its window
  and runs nothing. Use **Bundle > Open Bundle...** to try another file.
- If an app's script makes a mistake, a short message such as **Script
  error in fn 2.** appears and that action stops. The number says which
  function of the script stopped. **Bundle Info** shows the whole message,
  for example **Script error in fn 2: divide by zero.**
- If a script runs for a long time without finishing, Weave asks **Script
  is still running. Stop it?** Click **Yes** to stop it, or **No** to let it
  carry on.

## Files

Weave opens `.WAB` files, and an app cannot read or write any other file.
An app can save its own state, such as a high score: it goes into the
`SYSTEM/APPDATA` folder of the disk Weave was started from, in a file named
after the app with `.SAV` on the end. Two apps whose files have the same
name share one save file. None of the three demos saves anything.

## Good to know

- Weave apps show colour only in a game area such as Pong's field, and only
  on a VGA or EGA screen. Everything else is black and white on every
  screen.
- A game area reads the letters, the digits and the space bar. The arrow
  keys do not reach it, which is why Pong uses `A` and `Z`.
- Weave does not run on a 128KB machine.
- To make your own app, or change one of these, use Loom ([[LOOM.O88]]). The sources of
  all three demos are in the `LOOM` folder.
