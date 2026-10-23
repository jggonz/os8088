# Chart

Chart draws a chart from a spreadsheet file. It opens a file that [[SHEET.O88]] (or
another spreadsheet) saved, takes a column of its numbers, and draws them as
a column, bar, line, area, pie, scatter or combination chart. You can save
the chart as a BMP picture to use in Paint, PiXEL or a document.

![Chart showing the monthly units from the sample sales file.](shot:chart-window){full}

## Starting Chart

Chart is on the Office disk, at the top level of the disk. On the larger
applications disks it is in the `APPS` folder. Double-click **CHART.O88**.
The window opens empty; choose **File > Open...** to pick a file. You can
open more than one Chart at a time, each with its own file.

Double-clicking a spreadsheet file opens it in Sheet, not in Chart. To chart
a file, start Chart and open the file from its File menu.

## From Sheet to Chart

Chart reads files, so a chart of your own figures starts in Sheet:

1. In Sheet, put the numbers in a column. A heading above them is fine:
   labels are skipped.
2. Save the sheet with **File > Save As...** as **Normal**, **SYLK** or
   **DIF**, so the file's name ends in `.BIF`, `.SLK` or `.DIF`.
3. Start Chart, choose **File > Open...** and pick the file.

Sheet can also draw a chart of the selected cells itself, with **Data >
Chart Column...**. Use that while you are still working on the figures, and
Chart when you want to look at a saved file without opening Sheet.

## Choosing the column

Chart picks the column itself at first: the leftmost column that holds
numbers. For the sample [[SALES.SLK]] that is the units, column B.

The **Data** menu chooses instead. **Column A** to **Column H** chart that
column, and **Automatic** goes back to the leftmost one. Chart reads the
file again from the disk when you choose, so keep the disk in the drive. If
the column holds no numbers, Chart says `No data in that column.`

**Named ranges.** When the file has names for blocks of cells, made in Sheet
with **Formula > Define Name...** and saved as SYLK or Normal, the Data menu
lists those names first, in place of Column A, Column B and so on. Choose a
name to chart that block. The items after the last name keep their column
labels.

Opening another file goes back to Automatic, and the Data menu shows that
file's names.

## Choosing the chart type

The **Gallery** menu changes how the numbers are drawn. The chart redraws at
once.

![Revenue, column C, as a line chart.](shot:chart-line){full}

| Item | What it draws |
|---|---|
| Area | a line with the space below it filled |
| Bar | horizontal bars, one per value |
| Column | vertical columns, one per value. This is the first type you see |
| Line | the values joined by a line |
| Pie | a circle cut into slices, with a key of the slices' values beside it |
| Scatter | a mark for each value; see below |
| Combination | columns, with a line over them; see below |

![The same figures as a pie.](shot:chart-pie){right}

Scatter and Combination use **two** columns: the one you chose and the next
column of numbers to its right. Scatter puts a mark at each pair, the first
column across and the second up. Combination draws the first as columns and
the second as a line, each to its own scale, so units and revenue can share
one chart. With only one column of numbers, Scatter marks each value against
its position and Combination is a plain column chart.

![Units as columns and revenue as a line: the Combination type on the sample file.](shot:chart-combination){left}

The chart has the file's name as its title and the largest value at the top
of the scale. When some values are below zero, the chart puts zero in the
middle and draws them downwards.

## Saving the chart as a picture

**File > Export as BMP...** opens the Save dialog with the name
`CHART.BMP`. Change the name if you like and save. Chart says `Chart
exported.` The picture is the chart exactly as the window shows it, 240 by
160 pixels in 16 colours, and [[PAINT.O88]] and [[PIXEL.O88]] can open it. If no file is
open yet, Chart says `No chart to export.`

## The menus

| Menu | Item | What it does |
|---|---|---|
| File | Open... | opens a spreadsheet file and charts it |
| File | Export as BMP... | saves the chart as a BMP picture |
| Gallery | Area, Bar, Column, Line, Pie, Scatter, Combination | chooses the chart type |
| Data | Automatic | charts the leftmost column of numbers |
| Data | Column A to Column H | charts that column. A file's named ranges take the first of these places |
| Chart | About Chart | shows who made it |
| Chart | Close | closes this Chart |

## Files

Chart opens three of the formats Sheet writes, and decides which by the
file's extension:

- `.SLK`, SYLK
- `.DIF`, DIF
- `.BIF`, Excel's BIFF format, which Sheet calls Normal

It does not read `.XLS`, CSV, text or dBASE files. To chart one, open it in
Sheet and save it under a name ending in `.BIF`.

It saves only BMP pictures. It never changes the spreadsheet file.

## Good to know

- Chart shows up to 40 values from a column.
- It reads files up to 32KB. If there is not enough memory to read a file, it
  says `Not enough memory.`
- A file with no numbers in it gives `No numeric data found.`, and a file
  Chart cannot make sense of gives `Could not read that file`.
- Only columns A to H, or up to eight named ranges, can be picked from the
  Data menu.
- From a workbook with several sheets, Chart reads the first sheet.
- Chart cannot edit numbers. Change them in Sheet, save, and open the file
  again.
