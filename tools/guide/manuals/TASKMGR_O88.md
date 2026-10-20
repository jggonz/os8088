# Task Manager

Task Manager shows what the machine is doing right now: how busy the
processor is, how much memory is in use, and which programs are running,
with each one's share of the processor and of the memory. A second page
draws a map of memory, and a third lists every block of memory that has been
handed out and who holds it.

Task Manager only watches. It cannot stop or close a program; close a program
from its own window as usual.

![Task Manager with Note Pad and Calculator running.](shot:taskmgr-window){full}

## Starting Task Manager

![The System menu, at the left end of the menu bar.](shot:taskmgr-sysmenu){left}

Click the chip at the left end of the menu bar and choose **Task Manager**.
This works from any program. If Task Manager is already open, the menu brings
it to the front rather than opening a second one.

Task Manager is a program on the system disk, in the `SYSTEM` folder, so the
system disk has to be in drive A: when you open it. If it is not, the menu
bar says **No system disk in A:**. If the program has gone from the folder it
says **Not in SYSTEM**, and if memory is too short, **Out of memory**. You
can also double-click **TASKMGR.O88** in the `SYSTEM` folder, which opens a
new one each time.

It always opens on the first page.

## Changing page

Click anywhere inside the window to go to the next page. The three pages come
round in turn: the processor and program list, the memory map, and the list
of memory blocks, then back to the first. The figures keep being measured
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
kilobytes (K). It counts everything: the system itself, the room set aside
for programs, and every extra block a program has asked for. The bar under it
shows the same figure. **HEAP** is the part of memory programs and the system
ask for blocks from: how much of it is in use, out of its size.

The list has one line per running program:

| Column | What it shows |
|---|---|
| NAME | The program. **IVT+Pad** is the first line: 2K at the very bottom of memory that belongs to the machine's BIOS, with a half-K beside it nothing can use. **System** is os8088 itself; built-in windows such as a Disk window are listed under it, indented |
| ST | What it was doing at the last reading (see below) |
| CPU | Its share of the processor's work over the last half second |
| MEM | The memory it holds, in K: its own room plus any extra blocks it asked for |

The CPU figures add up to 100% between them, counting System. Lines with
dashes are free places for more programs. IVT+Pad, System and the heap add
up to all of the machine's memory, and the MEM figures add up to RAM used.
On a screen too short for every line, such as a CGA, IVT+Pad is counted
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

## The second page: the memory map

![The memory map page.](shot:taskmgr-memory){right}

The top line repeats the **RAM** and **HEAP** figures. Under it is a bar that
stands for all of the machine's memory, from the bottom on the left to the top
on the right. The grey part is os8088 itself, the striped part is the system's
own buffers, each program's room is drawn in its own pattern, and every extra
block a program asked for is a small framed box. White is free.

The next line names the processor. On a machine with memory above the first
megabyte, an **XMS** line and bar show how much of that is in use.

The list under it has four columns. **ADDR** is where the item starts in
memory, as a hexadecimal number. **SIZE** is how big it is, and **HEAP** is the
extra memory it has asked for on top.

- **IVT+Pad** is the 2K at the very bottom: the area that belongs to the
  machine's BIOS, and a half-K beside it that nothing can use.
- **System** is os8088, split into its program and data (**Code+data**), the
  working space of the running programs (**Stacks**) and its copy of the
  disk's file table (**FAT snap**).
- **Builtins** are the system's own windows, which have no memory of their own
  beyond the blocks they ask for.
- **Packages** is the room for loaded programs, how much is used out of all
  of it, followed by one line per program. The small square in front of each
  program matches its pattern in the map.

This page and the next are redrawn about every two seconds.

## The third page: memory blocks

![The memory blocks page.](shot:taskmgr-heap){full}

This page lists every block of memory that has been handed out, grouped
under the program that holds it, with the system's own first.

The three lines at the top sum it up:

| Figure | Means |
|---|---|
| HEAP | The size of the area blocks come from |
| AVAIL | How much a new request could get, counting caches the system would give up |
| HELD | Memory held by blocks that stay until their owner lets them go, and how many blocks |
| PURGE | Memory held by caches the system can drop when room is needed, and how many |
| MAX RUN | The biggest single block a program could get in one piece |
| FRAG | The rest of AVAIL, outside that biggest piece |

When a program says it is out of memory although AVAIL looks large, MAX RUN
is the figure to look at: the free memory is there, but in pieces.

Each block shows its **TYPE**, its address (**ADDR**) and its **SIZE**. Some
of the types you will see:

| TYPE | What it is |
|---|---|
| Region | A program's own room |
| Data | A block a program asked for, such as a document or a picture |
| WinSave | A copy of a window's picture, kept so it can be brought to the front quickly |
| DirRead, FATwin | Disk caches that make folders open faster |
| Icons | The desktop's store of program icons |
| DrvImg, DrvBuf | A driver, and the memory it uses |
| MenuSav | What was under an open menu |

**TIER** says what happens to the block when memory runs short. **HELD**
blocks stay. The others are caches the system can drop, starting with
**TRIV** (cheap to rebuild), then **LOW**, **MED** and last **HIGH** (slow to
rebuild, so it is kept longest).

When the list is longer than the window, use its scroll bar.

## The TaskMgr menu

Task Manager has no menus of its own. The menu at the left of the bar,
**TaskMgr**, has **About** and **Close**. The About card reads *Task Manager
for os8088, Contributed by Elendilon*.

## Good to know

- Task Manager needs about 7K of free memory to open.
- The CPU figure is measured, not estimated: it is the time the machine spent
  doing nothing, taken away from the total.
- A program that hires a background worker shows **run**, **rdy** or **slp**.
  One that only answers your clicks and keys shows **evt**, and its CPU share
  is the time spent drawing and answering.
- The window sizes itself to the screen when it opens. On a short screen,
  such as a CGA's, the list runs on in a second column.
- On the small 128KB system, Task Manager has only the first page.
