# Desk Calendar

Desk Calendar lives on the desktop itself, under every window. It shows
today's date, the month, the appointments of the day you pick, a list of
tasks to tick off, and the next reminder. It stays where you put it, and once
it is open it comes back by itself every time the machine starts.

![Desk Calendar on the desktop, with a reminder ringing on its bottom card.](shot:deskcal-desktop){full}

## Starting Desk Calendar

Desk Calendar is on the system disk, in the `STIKAPPS` folder. Double-click
**DESKCAL.O88** to open it. You can also pick **Desk Calendar** from the
**Sticky Apps** menu on the desktop's menu bar (see below).

Only one Desk Calendar can be open. If you try to open a second, the menu bar
says **Desk Calendar is open**.

It opens near the top right of the screen as three white cards with the
desktop showing between them, and a name tag, **Desk Calendar**, under them.
It has no title bar and no dock tile. Windows always cover it, never the other
way round.

## The three cards

![The three cards: the month, the day and its tasks, and the next reminder.](shot:deskcal-widget){right}

**The month.** The big number at the top is today's date, with the weekday
and month beside it. Under it is a month of days, with the week starting on
Monday. Today is drawn white on black. The day you have selected, if it is
not today, has a box round it. A small dot under a number means that day has
appointments.

Click `<` or `>` to see the previous or next month. Click the month's name,
or the big date at the top, to come back to today. Click a day to select it.

**The day.** The second card shows the selected day. Its heading reads
**Today**, or the date, such as `Tue 20 Oct`. Under it are that day's
appointments, each with its time (`--:--` for an all-day one), or **No
appointments**. Under the line is your task list, which is one list for all
days. Click a task's box to tick it or clear the tick.

The card has room for two appointments and two tasks. When a list is longer,
its last line reads `+2 more` (or however many are not shown). Click that
line to see the next page.

**The reminder.** The third card shows a bell and the next reminder due
today, with its time. When there is nothing left today it says **No
reminders today**.

## Adding appointments and tasks

Click the `+` at the right of the day card's heading. A typing line opens in
its place. Type the entry and press `Enter` to keep it, or `Esc` to throw it
away.

What you type decides what it becomes:

| You type | It becomes |
|---|---|
| `9:30 Dentist` or `09:30 Dentist` | an appointment at 09:30 on the selected day |
| `930 Dentist` or `0930 Dentist` | the same: three or four digits are a time |
| `Dentist` | an all-day appointment |

![The typing line open on the day card.](shot:deskcal-entry){left}

Times are 24-hour: `14:00`, not `2:00`. A first word that is not a real time,
such as `24:00` or `3 apples`, is taken as part of the text.

To add a task instead, press `K` (or choose **Calendar > New Task**) and type
it. `N` (or **New Appointment**) opens the line for an appointment.

An appointment's text can be up to 25 letters, a task's up to 23. Longer text
is refused with **At most 25 letters** or **A task: at most 23**, and the line
stays open so you can shorten it. The cards show only the first part of a
long entry.

## Changing and deleting

![The Calendar menu.](shot:deskcal-menu){right}

Click an appointment or a task to select it; it turns white on black. Click it
again, or press `Enter`, to edit it in the typing line.

To delete the selected item, press `Del` (or choose **Delete Item**). The menu
bar says **Delete again to confirm**. Press `Del` again straight away to
delete it. Anything else in between cancels.

**Clear Done Tasks** removes every ticked task at once, without asking.

## Reminders

![The Remind menu.](shot:deskcal-remind-menu){right}

A timed appointment gets a reminder when you make it. By default the reminder
goes off at the appointment's own time. The **Remind** menu changes how early.

If an appointment is selected, your choice applies to that appointment. If
not, it becomes the default for new appointments. The menu bar says which.
**No Reminder** turns reminders off for that appointment, or for new ones.
All-day appointments never have a reminder.

![A reminder ringing on the third card.](shot:deskcal-ringing){left}

When a reminder goes off, the speaker chirps, the menu bar shows the time and
text (for example `09:30 Dentist`), and the third card rings: it shows the
appointment with **Snooze** and **Dismiss** under it. Nothing pops up and
nothing stops you working.

- **Snooze** (or `S`) rings it again in five minutes.
- **Dismiss** (or `D`) puts it away. If another reminder is waiting, the card
  shows that one next.

If the calendar was not running when one of today's reminders was due, it
rings it as soon as it opens and the menu bar tells you how many you missed,
for example `3 missed reminders`. Reminders from earlier days are not
replayed.

## Moving and closing it

![The name tag turns into a handle, with a close box at its left.](shot:deskcal-handle){right}

Point at the calendar for a moment and its name tag turns into a black handle
with a close box at the left and a grip at the right. Drag the tag, or the
desktop showing between the cards, to move the calendar anywhere on the
desktop. It remembers the place.

To close it, click the close box on the handle. You can also click a card to
give the calendar the menu bar and choose **Close** from the **Desk Calendar**
menu at the left of the bar.

Closing it is the only thing that stops it coming back. If it is open when
you restart or switch off, it opens again at the next start, in the same
place, with all your entries.

## The menu bar and the keyboard

The calendar's menus show while it has the menu bar. Click any card to give
it the bar. Press `Esc` to hand the bar and the keyboard back to the window
in front.

| Item | Key | What it does |
|---|---|---|
| New Appointment | `N` | Opens the typing line for an appointment on the selected day |
| New Task | `K` | Opens the typing line for a task |
| Edit Item | `Enter` | Edits the selected appointment or task |
| Delete Item | `Del` | Deletes the selected item, after a second press |
| Clear Done Tasks | | Removes every ticked task |
| Go to Today | `T` | Shows this month and selects today |
| Previous Month | `PgUp` or `,` | Shows the month before |
| Next Month | `PgDn` or `.` | Shows the month after |

Items that cannot be used right now are greyed. The calendar holds 48
appointments and 32 tasks. When it is full, **New Appointment** greys and `N`
says **Calendar is full (48)**; at 32 tasks, `K` says **Task list is full
(32)**.

Other keys:

| Key | What it does |
|---|---|
| Left, Right | The day before or after |
| Up, Down | The same day a week earlier or later |
| `Space` | Ticks or clears the selected task |
| `S`, `D` | Snooze or dismiss a ringing reminder |
| `Esc` | Gives the menu bar back |

The **Desk Calendar** menu at the left of the bar has **About Desk
Calendar** and **Close**.

## The Sticky Apps menu

![The Sticky Apps menu, with Desk Calendar ticked On.](shot:deskcal-stickymenu){right}

Desk Calendar is a Sticky App: a small program that belongs to the desktop.
When no window is in front, the desktop's menu bar has a **Sticky Apps**
menu ([[STIKMENU.DRV]]). It lists every Sticky App on the system disk. Pick one marked `Add` to
open it, or one ticked `On` to close it.

- **Arrange Sticky Apps...** lines the open ones up down the right of the
  screen.
- **Hide All Sticky Apps** hides them without closing them. The same item
  then reads **Show All Sticky Apps**.
- **More Sticky Apps...** opens [[THEWIRE.O88]], the online software library.

## Files

The calendar keeps its entries and its place on the screen in
`SYSTEM\APPDATA\DESKCAL.DAT` on the disk it was started from. It saves by
itself a moment after each change; there is no Save command. If it was
started from the system disk and that disk is out of the drive, it waits and
saves when the disk is back.

If the file is damaged, the menu bar says **Calendar file damaged** and the
calendar opens empty. The damaged file is replaced the first time you change
something.

## Good to know

- Times are always shown and typed in 24-hour form, even if the menu bar's
  clock is set to 12-hour.
- A machine without a clock card starts with the wrong date. Set the date and
  time in the Control Panel first. Without a clock card the calendar warns
  **No RTC - check the time** when a reminder is still to come today.
- After moving the calendar, wait a few seconds before switching off, or it
  may come back where it was before.
- On a machine without the desktop layer (the small 128KB system), Desk
  Calendar opens as an ordinary window with a title bar, and it does not come
  back by itself at the next start.
- **Desk Calendar > About Desk Calendar** shows its name on the menu bar.
