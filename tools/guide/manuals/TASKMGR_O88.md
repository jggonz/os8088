# Task Manager

Task Manager shows what the machine is doing right now: how busy the
processor is, how much memory is in use, and which programs are running,
with each one's share of the processor and of the memory. A second page
draws a map of memory and lists who holds it, down to every block that has
been handed out.

Task Manager only watches. It cannot stop or close a program; close a program
from its own window as usual.

![Task Manager with Note Pad and Calculator running.](shot:taskmgr-window){full}

## Starting Task Manager

![The System menu, at the left end of the menu bar.](shot:taskmgr-sysmenu){left}

Click the chip at the left end of the menu bar and choose **Task Manager**.
This works from any program. If Task Manager is already open, the menu brings
it to the front rather than opening a second one.

Task Manager is a program in the `SYSTEM` folder of the disk os8088 started
from: drive A: after a floppy boot, or the hard-disk partition it booted
from. That disk has to be there when you open it. If it is not, the menu bar
says **No system disk in A:**, with that drive's letter. If the program has gone from the folder it
says **Not in SYSTEM**, and if memory is too short, **Out of memory**. You
can also double-click **TASKMGR.O88** in the `SYSTEM` folder, which opens a
new one each time.

It always opens on the first page.

## Changing page

Click anywhere inside the window to go to the other page: the processor and
program list, then the memory page, then back to the first. On the memory
page, a click in the list or on its switches does what that item says
instead; click above the list to go back. The figures keep being measured
while you look at another page, so the graph has no gap when you come back.

## The first page: the processor and the programs

**CPU** is how busy the processor was over the last half second, from 0% (doing
nothing, waiting for you) to 100% (busy all the time). Next to it, **SCH
preempt** means the system shares the processor out on a timer, so a busy
program cannot hold up the others. **SCH coop** means each program runs until
it gives way. You choose between them in the Control Panel; Task Manager only
shows which is in use.

The graph under it is the processor's history, a column every half second.
It sweeps from left to right like an oscilloscope and starts again at the
left. The white column is where the next reading will go. The taller the
black, the busier the machine was.

**RAM** is how much of the machine's memory is in use, out of all it has, in
kilobytes (K), or in megabytes (M) on a machine with memory above the first
megabyte. It counts everything: the system itself, the room set aside for
programs, every extra block a program has asked for, and the memory above
the first megabyte when the machine has any. The bar under it
shows the same figure. **HEAP** is the part of memory programs and the system
ask for blocks from: how much of it is in use, out of its size.

The list has one line per running program:

| Column | What it shows |
|---|---|
| NAME | The program. **IVT+BIOS** is the first line: 2K at the very bottom of memory that belongs to the machine's BIOS, with a half-K beside it nothing can use, and, on machines whose BIOS keeps its own area at the top of memory, that area too. **System** is os8088 itself, including what it holds above the first megabyte; built-in windows such as a Disk window are listed under it, indented |
| ST | What it was doing at the last reading (see below) |
| CPU | Its share of the processor's work over the last half second |
| MEM | The memory it holds, in K: its own room plus any extra blocks it asked for |

The CPU figures add up to 100% between them, counting System. Lines with
dashes are free places for more programs. The MEM figures add up to RAM used.
On a screen too short for every line, such as a CGA, IVT+BIOS is counted
into System rather than pushing a program off the list.

The ST column:

| ST | Means |
|---|---|
| run | It was running at that moment |
| rdy | It has work in the background and is waiting its turn |
| slp | It is waiting for time to pass, such as a clock waiting for the next second |
| evt | It has no background work. It only runs when you click, type or it needs redrawing |
| die | It is closing |

Task Manager is itself on the list as **TaskMgr**, so you can see what
watching costs.

## The second page: memory

![The memory page.](shot:taskmgr-memory){right}

The top line repeats the **RAM** and **HEAP** figures. Under it is a bar that
stands for the first megabyte of memory, from the bottom on the left to the
top on the right. The grey part is os8088 itself, the striped part is the
system's own buffers, each program's room is drawn in its own pattern, and
every extra block a program asked for is a small framed box. White is free.

The next line names the processor. On a machine with memory above the first
megabyte, an **XMS** line and bar show how much of that is in use. On the
protected-mode build of os8088 for the 286 and later, memory above the first
megabyte is ordinary memory that programs run in: **Ram** counts all of it, in
megabytes, and there is no XMS bar.

**AVAIL** is how much memory a new request could get, counting the caches the
system would give up for it. **MAX RUN** is the biggest single block a program
could get in one piece. When a program says it is out of memory although AVAIL
looks large, MAX RUN is the figure to look at: the free memory is there, but
in pieces. On the protected-mode build both count the memory above the first
megabyte too, and read in megabytes once they pass 9,999K.

### The list

Each line of the list is something that holds memory, with its total in
**SIZE**. A line with **+** in front opens when you click it, showing what it
is made of, and **-** closes it again. **+ALL** at the top right opens every
line, and **-ALL** closes them all.

- **System** is os8088 itself. Opened, it shows the 2K at the very bottom
  that belongs to the machine's BIOS (**IVT+BIOS**), its program and data
  (**Code+data**), the working space of the running programs (**Stacks**),
  its copy of the disk's file table (**FAT snap**), and what it holds above
  the first megabyte. Under those come its own lines that open in turn:
  **Kernel** for blocks the system asked for, one line per driver, named by
  its file (**SOUND.DRV**, **XMEM.DRV**), and one per system window, such as
  a Disk window, named by its folder.
- **Programs** is the total of every program, and each program follows it on
  a line of its own. The small square beside it matches its pattern in the
  map. Opened, a program shows its own room and every extra block it asked
  for.

With every line closed, the sizes add up to the memory **RAM** says is in
use, and that plus **AVAIL** is all of it.

Each block in an opened line shows its **TYPE**, its address (**ADDR**) and
its **SIZE**. **HIGH** in ADDR means the block is above the first megabyte.
Some of the types you will see:

| TYPE | What it is |
|---|---|
| Region | A program's own room |
| Data | A block a program asked for, such as a document or a picture |
| DrvImg, DrvBuf | A driver, and the memory it uses |
| CTRL, DISKOPS, ... | A part of the system loaded from its disk while you use it, such as the Control Panel |
| WinSave | A copy of a window's picture, kept so it can be brought to the front quickly |
| DirRead, FATwin | Disk caches that make folders open faster |
| Icons | The desktop's store of program icons |
| MenuSav | What was under an open menu |

**TIER** says what happens to the block when memory runs short. **HELD**
blocks stay. The others are caches the system can drop, starting with
**TRIV** (cheap to rebuild), then **LOW**, **MED** and last **HIGH** (slow to
rebuild, so it is kept longest).

### More detail

**+DETAIL** at the top left adds two more parts to the end of the list, and
**-DETAIL** takes them away:

- **Purgeable** is the caches: memory the system is using to go faster and
  gives up the moment something asks for room. It is in no total, and AVAIL
  already counts it as free.
- **640K-1MB** is the part of the first megabyte that is not memory: the
  **Video** window, each add-in card's **ROM** at the address it was found,
  and the machine's own **BIOS**. **Unrep.** is the rest of that range, which
  nothing on the machine describes.

When the list is longer than the window, use its scroll bar. This page is
redrawn about every two seconds.

## The TaskMgr menu

Task Manager has no menus of its own. The menu at the left of the bar,
**TaskMgr**, has **About** and **Close**. The About card reads *Task Manager
for os8088, Contributed by Elendilon*.

## Good to know

- Task Manager needs about 12K of free memory to open.
- The CPU figure is measured, not estimated: it is the time the machine spent
  doing nothing, taken away from the total.
- A program that hires a background worker shows **run**, **rdy** or **slp**.
  One that only answers your clicks and keys shows **evt**, and its CPU share
  is the time spent drawing and answering.
- The window sizes itself to the screen when it opens. On a short screen,
  such as a CGA's, the list runs on in a second column.
- On the small 128KB system, Task Manager has only the first page.
