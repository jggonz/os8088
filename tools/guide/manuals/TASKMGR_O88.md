# Task Manager

Task Manager shows what the machine is doing right now: how busy the
processor is, how much memory is in use, and which programs are running,
with each one's share of the processor and of the memory. A second page
draws a map of memory and lists every block of memory that has been handed
out, grouped under whoever holds it.

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

Click anywhere inside the window to go to the next page. The two pages take
turns: the processor and program list, then the memory page, then back to
the first. On the memory page a click on the list, its header or its scroll
bar works the list instead (see below). The figures keep being measured
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

## The second page: the memory map

![The memory map page.](shot:taskmgr-memory){right}

The top line repeats the **RAM** and **HEAP** figures. Under it is a bar that
stands for all of the machine's memory, from the bottom on the left to the top
on the right. The grey part is os8088 itself, the striped part is the system's
own buffers, each program's room is drawn in its own pattern, and every extra
block a program asked for is a small framed box. White is free.

The next line names the processor. On a machine with memory above the first
megabyte, an **XMS** line and bar show how much of that is in use.

On the protected-mode build of os8088 for the 286 and later, memory above the
first megabyte is ordinary memory that programs run in. There **RAM** counts
all of it, in megabytes; the line under the processor reads **HIGH**, how much
of that memory is in use out of all of it; and a program running up there
reads **HIGH** in **ADDR** and is not drawn on the bar, which is the first
megabyte only. Its **MEM** and **HEAP** still count everything it holds.

Under that, **AVAIL** is how much memory a new request could get, and
**MAX RUN** the biggest single block it could get in one piece. When a
program says it is out of memory although AVAIL looks large, MAX RUN is the
figure to look at: the free memory is there, but in pieces.

The list under it has one line per owner, with its total. Click an owner's
line to open it into its blocks, and click it again to close it. **ADDR** is
where an item starts in memory, as a hexadecimal number, and **SIZE** is how
big it is. The small square in front of a program matches its pattern in the
map.

The header above the list has two switches, drawn in reverse:

- **PROGRAMS** or **SYSTEM**, at its left end, changes the view. **PROGRAMS**
  lists each program on its own line and the system as one line. **SYSTEM**
  opens the system out at the top and puts the programs together under one
  **Programs** line.
- **+ALL** at its right end opens every line, and **-ALL** closes them again.

What you will see under the system:

- **IVT+BIOS** is the 2K at the very bottom: the area that belongs to the
  machine's BIOS, and a half-K beside it that nothing can use. A BIOS that
  keeps an area of its own at the top of memory adds it here.
- **Code+data** is os8088's program and data, **Stacks** the working space
  of the running programs, and **FAT snap** its copy of the disk's file
  table. On a machine with memory above the first megabyte, what it holds
  there is **Above 1MB**.
- **Kernel**, a line for each driver (such as **SOUND.DRV**) and a line for
  each of the system's own windows (a Disk window is named by its folder)
  open into the blocks each one asked for.

**Purgeable** gathers the caches the system can drop when room is needed,
whoever they were made for. An open block shows its **TYPE** and its
**TIER**. Some of the types you will see:

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

**640K-1MB**, after the list, is the part of the first megabyte that is not
memory: the **Video** window, each add-in card's **ROM** at the address it
was found, and the machine's own **BIOS**. **Unrep.** is the rest of that
range, which nothing on the machine describes. It is listed so you can see
what is there, and it is not counted in RAM.

When the list is longer than the window, use its scroll bar. The page is
redrawn about every two seconds.

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
