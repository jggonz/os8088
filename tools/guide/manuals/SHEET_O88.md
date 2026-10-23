# Sheet

Sheet is a spreadsheet in the style of Excel 2.1. A workbook holds four
sheets, and each sheet is a grid of cells, 256 columns (A to IV) by 16,384
rows. A cell holds a number, a label, a logical value (TRUE or FALSE) or a
formula. Formulas work out totals, averages, dates, loan payments, lookups
and much more from other cells, and update when those cells change.

You can format numbers as currency, percentages, dates or times, frame cells
with borders, keep a table of records and search it, sort rows, chart a
block of figures, and record and run macros. Sheet opens and saves Excel
files as well as SYLK, DIF, CSV, text and dBASE files.

![Sheet with the sample sales figures open. Column C holds formulas that multiply each month's units by 25.](shot:sheet-window){full}

## Starting Sheet

Sheet is on the Office disk, at the top level of the disk. On the larger
applications disks it is in the `APPS` folder. Double-click **SHEET.O88** to
start with an empty workbook, or double-click a spreadsheet file (`.SLK`,
`.DIF`, `.BIF` or `.XLS`) to open Sheet on that file. The Office disk has a
sample in its `MEDIA` folder, [[SALES.SLK]].

Two more files sit beside SHEET.O88 and must stay in the same folder:
[[CHART.OVL]], which holds Sheet's dialogs, its file formats, its charts and
many of its functions, and [[MACRO.OVL]], which runs macros. Sheet reads both
when it starts.

Sheet needs about 100KB of free memory to open, and about 100KB more for
CHART.OVL, MACRO.OVL and Undo. If the machine cannot spare the first 100KB,
Sheet does not open. If it cannot spare the rest, Sheet opens without the
parts that did not fit: without room for Undo, the Edit menu says `Can't
Undo`; without CHART.OVL, see "Good to know" below.

## The window

From the top down, the Sheet window has:

- its own **menu bar**: File, Edit, Formula, Format, Data, Options, Macro,
  Sheets and Help. These menus are inside the window, not in the bar at the
  top of the screen. Press on a title, move to an item and let go.
- the **reference box** on the left, naming the selected cell (`C2`), and the
  **formula bar** beside it, showing what the cell really holds. For a
  formula cell that is the formula, not its result.
- the **grid**, with column letters across the top and row numbers down the
  side, and scroll bars on the right and bottom.
- the **status line**, which says `Ready` or reports what the last command
  did. On the right it shows `NUM` and, when calculation is manual and the
  sheet needs recalculating, `CALCULATE`.

The title bar names the workbook, as in `Sheet - SALES.SLK`. Drag the
window's corner to make it bigger or smaller.

The menu bar at the top of the screen carries a **Sheet** menu with **About
Sheet** and **Close**.

## Moving around and selecting

Click a cell to select it. The arrow keys move one cell, **Page Up** and
**Page Down** move a screenful, and **Home** goes back to column A. Use the
scroll bars or the mouse wheel to look elsewhere without moving the
selection, or **Formula > Goto...** to jump straight to a cell by typing its
reference, such as `B12`.

To select a block, drag across it with the mouse, hold **Shift** and click
the far corner, or hold **Shift** and press the arrow keys. Most commands
work on the whole block: formatting, Clear, Copy, Cut, Fill, Sort and charts.

## Typing into cells

Select a cell and start typing. What you type appears in the formula bar.
Press **Enter** to store it and move down, or **Tab** to store it and move
right. After a run of Tabs, Enter goes to the start of the next row, in the
column where the run began, so you can type a table row by row. The up and
down arrows also store the entry and move.

While you type, Left, Right, Home, End, Delete and Backspace edit the text in
the formula bar. **Esc** throws the entry away. To change a cell that already
has something in it, select it and press **F2**. **Delete**, pressed when you
are not typing, empties the selected cell.

Sheet decides what a cell holds from what you type:

- text starting with `=` is a **formula**;
- a complete number, such as `42`, `-7` or `3.5`, is a **number**;
- `TRUE` or `FALSE`, in any case, is a **logical value**;
- anything else, such as `Sales` or `3kg`, is a **label**.

Numbers sit on the right of the cell, labels on the left, and logical values
and error values in the middle. A label longer than its column runs on into
the empty cells beside it. A number too wide for its column shows as `#`
signs; widen the column to see it.

## Formulas

A formula starts with `=` and can use numbers, text in double quotes, cell
references, the operators below, parentheses and functions. `=B2*25`,
`=SUM(B2:B13)`, `=IF(B2>100,"High","Low")` and `="Total: "&C14` are all
formulas.

![Typing a formula: the formula bar shows it as you type.](shot:sheet-formula){full}

| Operator | Meaning |
|---|---|
| `+` `-` `*` `/` | add, subtract, multiply, divide |
| `^` | power: `2^3` is 8 |
| `&` | joins text: `="a"&"b"` is `ab` |
| `=` `<` `>` `<=` `>=` `<>` | comparisons, which give TRUE or FALSE |
| `-` in front | negative: `-B2` |
| `A1:B5` | a range of cells |
| a space between two ranges | the cells the two ranges share: `SUM(A1:C9 B1:B5)` |

Powers are worked out first, then multiplication and division, then addition
and subtraction, then `&`, then comparisons. Use parentheses to change the
order.

**References.** `B2` names a cell on the same sheet and `Sheet2!B2` names a
cell on another sheet of the workbook. A `$` in front of the column or the
row, as in `$B$2`, keeps that part from changing when the formula is copied
or filled. While you type a formula, **F4** (Formula > Reference) steps the
reference at the cursor through `A1`, `$A$1`, `A$1` and `$A1`. Inserting or
deleting rows and columns updates every reference, so a formula keeps
pointing at the same data. A reference to a cell that was deleted shows
`#REF!`.

**Names.** Select a cell or a block and choose **Formula > Define Name...**
to give it a name, such as `RATE` or `SALES`. A formula can then say
`=B2*RATE` or `=SUM(SALES)`, and **Goto...** with a name selects the whole
block. Names can be up to 12 letters, are the same in upper and lower case,
and belong to the whole workbook. A workbook holds up to 16.

**Paste Function...** and **Paste Name...** list the functions and the
defined names. Pick one and click OK, and it goes into the cell you are
typing in (a formula starts if you were not typing). A function arrives with
its opening parenthesis.

![Formula > Paste Function... lists every function.](shot:sheet-functions){full}

When a formula has an opening parenthesis without its partner, Sheet keeps
what you typed as a label and says `Unbalanced ( ) - kept as text.`

## Functions

Sheet has the worksheet functions of Excel 2.1. Paste Function lists them
all. The families, with a few examples:

| Family | Functions |
|---|---|
| Arithmetic | `SUM`, `PRODUCT`, `ABS`, `SIGN`, `INT`, `TRUNC`, `ROUND`, `MOD`, `SQRT`, `POWER`, `FACT`, `RAND` |
| Logs and trigonometry | `EXP`, `LN`, `LOG`, `LOG10`, `PI`, `SIN`, `COS`, `TAN`, `ASIN`, `ACOS`, `ATAN`, `ATAN2` |
| Statistics | `AVERAGE`, `MIN`, `MAX`, `COUNT`, `COUNTA`, `VAR`, `VARP`, `STDEV`, `STDEVP`, and `LINEST`, `LOGEST`, `TREND`, `GROWTH` for fitting a line or curve |
| Logic | `IF`, `AND`, `OR`, `NOT`, `TRUE`, `FALSE`, `CHOOSE` |
| Text | `LEN`, `LEFT`, `RIGHT`, `MID`, `UPPER`, `LOWER`, `PROPER`, `TRIM`, `CLEAN`, `REPT`, `FIND`, `SEARCH`, `SUBSTITUTE`, `REPLACE`, `EXACT`, `CHAR`, `CODE`, `T`, `VALUE`, `TEXT`, `DOLLAR`, `FIXED` |
| Dates and times | `DATE`, `TIME`, `DAY`, `MONTH`, `YEAR`, `WEEKDAY`, `HOUR`, `MINUTE`, `SECOND`, `DATEVALUE`, `TIMEVALUE`, `NOW` |
| Lookup and reference | `VLOOKUP`, `HLOOKUP`, `LOOKUP`, `MATCH`, `INDEX`, `INDIRECT`, `ROW`, `COLUMN`, `ROWS`, `COLUMNS`, `AREAS` |
| Finance | `PMT`, `PV`, `FV`, `NPV`, `NPER`, `RATE`, `IPMT`, `PPMT`, `IRR`, `MIRR`, `SLN`, `SYD`, `DDB` |
| Database | `DSUM`, `DAVERAGE`, `DCOUNT`, `DCOUNTA`, `DMAX`, `DMIN`, `DPRODUCT`, `DSTDEV`, `DSTDEVP`, `DVAR`, `DVARP` |
| Information | `ISBLANK`, `ISNUMBER`, `ISTEXT`, `ISNONTEXT`, `ISLOGICAL`, `ISERROR`, `ISERR`, `ISNA`, `ISREF`, `NA`, `TYPE`, `N`, `ERROR.TYPE`, `CELL` |
| Matrices | `MMULT`, `MINVERSE`, `MDETERM`, `TRANSPOSE` |

A date is a number of days, with the time of day as the fraction, so
`=B2+7` is a week later. Give the cell a date format (Format > Number...) to
see it as a date. `NOW()` reads the machine's real-time clock; on a machine
without one it shows `#N/A`.

A database function takes a database range, a field and a criteria range,
as in `=DSUM(Database,"Amount",Criteria)`; see "The Data menu" below.

## Error values

When a formula cannot give a value, its cell shows an error instead. A
formula that uses that cell shows the error too, so you can trace a wrong
total back to its cause.

| Shows | Means |
|---|---|
| `#DIV/0!` | division by zero, for example by an empty cell |
| `#VALUE!` | a label used as a number, or a formula that does not make sense, such as `=1+` |
| `#NAME?` | a function or name Sheet does not know |
| `#NUM!` | a number out of range, such as `SQRT(-1)` |
| `#REF!` | a reference to a cell that was deleted |
| `#NULL!` | two ranges with a space between them that share no cells |
| `#N/A` | no value available, for example a lookup that found nothing |

`=IF(B1=0,0,A1/B1)` guards against `#DIV/0!`: the branch that is not chosen
does not count. `ISERROR` and `ISNA` test for errors.

## Formatting

The Format menu changes how cells look. Each command applies to every cell
in the selection, empty cells included, so a value typed there later takes
the format.

| Item | What it does |
|---|---|
| Number... | lists Excel's number formats: General, `0`, `0.00`, `#,##0`, `#,##0.00`, four currency formats with `$`, `0%`, `0.00%`, `0.00E+00`, dates such as `m/d/yy` and `d-mmm-yy`, and times such as `h:mm AM/PM` |
| Alignment... | **General** (numbers right, labels left), **Left**, **Center**, **Right** |
| Font... | **Normal**, **Bold**, **Underline**, **Bold, Underline** |
| Border... | tick any of **Outline**, **Left**, **Right**, **Top**, **Bottom** and **Shade** |
| Cell Protection... | **Locked**, **Unlocked**, **Locked, Hidden** or **Unlocked, Hidden**; see "Protecting a sheet" |
| Row Height... | the height of the selected rows, in points, from 7.3 to 43.7. The standard is 12.75. `0` hides the rows |
| Column Width... | the width of the selected columns, in characters, from 1 to 40. `0` hides the columns |
| Justify | rewraps the text in the left column of the selection to the selection's width |

![Format > Number... lists Excel's number formats.](shot:sheet-number){full}

A format changes only how a number looks. A cell showing `50%` still holds
`0.5`, and a formula using it gets `0.5`.

Border works on the whole selection. **Left**, **Right**, **Top** and
**Bottom** draw that side of the selected block, so **Outline**, which ticks
all four, draws a frame round it. **Shade** shades every cell in the block.
Lines already drawn inside the block are kept.

You can also change a width or height with the mouse: drag the right edge of
a column letter or the bottom edge of a row number. Dragging it shut hides
the column or row. To show a hidden column again, select across it and type
a width in Column Width....

**Justify** takes the labels in the left column of the selection and
rewraps them into lines as wide as the whole selection, one line per row. A
blank cell in that column starts a separate paragraph. Every cell in the
left column must be text or blank; otherwise Sheet says `Justify needs text
or blank cells.` If the text needs more rows than you selected, it says
`Select more rows - the text needs them.`

## The Edit menu

| Item | Key | What it does |
|---|---|---|
| Undo | Ctrl+Z | undoes the last entry or command and names it, as in `Undo Paste`. After that it reads `Redo`. When there is nothing it can undo it reads `Can't Undo` and is greyed |
| Cut | Ctrl+X | copies the selection to the clipboard, then empties it |
| Copy | Ctrl+C | copies the selection to the clipboard |
| Paste | Ctrl+V | pastes the clipboard with its top-left corner at the selected cell, with the cells' formats, borders and notes |
| Clear... | | asks **All**, **Formulas** or **Formats**. Formulas removes the contents and keeps the formatting; Formats does the opposite |
| Paste Special... | | pastes only part of what you copied: **All**, **Formulas**, **Values**, **Formats** or **Notes** |
| Paste Link | | puts a formula in each cell that points back at the cell it was copied from, such as `=D1` |
| Delete... | | asks **Row** or **Column**, and deletes the selected cell's row or column |
| Insert... | | asks **Row** or **Column**, and inserts one at the selected cell |
| Fill Right | Ctrl+R | copies the left column of the selection across the rest of it |
| Fill Down | Ctrl+D | copies the top row of the selection down the rest of it |

Undo goes back one step. It covers a typed entry, every command on this
menu, the Delete key, and Data > Sort, Parse and Justify. A format, a name, a
note, a macro, Data > Extract and Series, and opening or starting a workbook
cannot be undone, and after one of them Undo reads `Can't Undo`.

Copied cells go on the clipboard as text, with tabs between columns and a line
break after each row, so a table pastes into Word or Note Pad and lines up.
Formulas move with their references adjusted, so `=A1*B1` copied one row down
becomes `=A2*B2`. Fill adjusts references the same way. **Values** pastes
the results of formulas instead of the formulas. Paste Special and Paste Link
need cells copied in Sheet; otherwise Sheet says `Copy a cell or range
first.`

## The Formula menu

![The Formula menu, inside Sheet's own menu bar.](shot:sheet-menu){full}

| Item | Key | What it does |
|---|---|---|
| Paste Name... | F3 | lists the defined names and pastes one into the cell |
| Paste Function... | Shift+F3 | lists the functions and pastes one into the cell |
| Reference | F4 | while you type a formula, steps the reference at the cursor between relative and absolute. Greyed at other times |
| Define Name... | | names the selected cell or block |
| Note... | | opens a note for the selected cell, several lines long. Click OK to keep it |
| Goto... | Ctrl+G or F5 | moves the selection to the cell or name you type, such as `F4` or `SALES` |
| Find... | Ctrl+F | finds the next cell whose text contains what you type, upper or lower case alike. It looks at what cells show, so it finds the results of formulas. It goes round to the top again; it says `Not found.` when nothing matches |

## The Data menu

The Data menu works on tables of records: a block whose first row holds the
field names, such as `Name`, `Fruit` and `Amount`, with one record on each
row below.

| Item | What it does |
|---|---|
| Form... | shows one record at a time, with **Prev**, **Next**, **New**, **Delete** and **Close** |
| Find | selects the next record that matches the criteria. The item then reads **Exit Find**; choose Exit Find and then Find again to go on to the next match |
| Extract... | copies the matching records into the selected range. Asks **All Matching Records** or **Unique Records Only** |
| Delete | deletes every matching record, after asking `Delete every matching record?` |
| Set Database | names the selection `Database` |
| Set Criteria | names the selection `Criteria` |
| Sort... | sorts the rows of the selection |
| Series... | fills the selection with a series of numbers or dates |
| Table... | fills a what-if table from a formula |
| Parse... | splits lines of text into columns |
| Chart Column... | opens a chart of the selection |
| Chart Gallery... | chooses the chart type |
| Export Chart as BMP... | saves the chart as a picture |

**A database.** Select the table, field names included, and choose **Set
Database**. Then build a criteria range somewhere else: field names in its
first row, and the values to look for beneath them. `Apple` matches text
starting with Apple, `*an*` matches text containing `an`, and `>100` or
`<>0` compare numbers. Conditions on one row must all hold; a record that
matches any one row counts. Select it and choose **Set Criteria**. Find,
Extract, Delete and the database functions all use these two ranges. Form
needs only the database.

In the form, Tab and Shift+Tab move between fields and Enter goes to the
next record. A field you type in is written back to its cell. **New** adds a
record below the database, which needs the row below it empty. The form's
**Delete** asks `Delete the displayed record?` first, and Data Delete asks
too, because it cannot be undone.

For Extract, select the extract range with the field names you want in its
first row. A range of only that row takes as many rows below it as the
records need; a taller range takes what fits and says `The extract range is
too small.` Extract copies values, not formulas.

**Sort...** first asks for the **1st Key:**, a cell in the column to sort by
(it starts on the selected cell), then **Ascending** or **Descending**. It
sorts the rows of the selection and moves the other columns of each row with
them, so a table stays together. In ascending order numbers come first,
then text, then logical values, then errors; Descending reverses that. Rows
with an empty key go last either way. With one cell selected it sorts that
whole column.

**Series...** asks **Linear**, **Growth**, **Date: Day**, **Date: Weekday**,
**Date: Month** or **Date: Year**, then the **Step value:**. Put the starting
value in the first cell of each row or column and select the range to fill.
A selection taller than it is wide fills down, otherwise across. Weekday
skips Saturdays and Sundays.

**Table...** fills a what-if table. Select the whole table: the formulas or
the formula, and the values to try along the top row and left column. Sheet
asks for the **Row Input Cell:** and then the **Column Input Cell:**, the
cells the values are put into in turn; leave one empty for a table with one
set of values. The results cannot be edited one at a time; clear the whole
table to remove it.

**Parse...** splits text that lines up in columns, such as a printed report
pasted into one column. Select the column. The dialog shows the first line
in **Parse line:**; put `[` and `]` round each field, or press **Guess** to
let Sheet bracket fields that are separated by two or more spaces. **Clear**
removes the brackets. OK splits every selected cell into the columns to its
right.

### Charts

**Chart Column...** opens a chart of the selection in a window of its own.
The left column of the selection gives the values, and labels are skipped.
With one cell selected it charts that whole column. If the next column to
the right holds numbers too, it becomes a second series for the Scatter and
Combination types. The chart is live: change a number in the charted cells
and the chart changes with it. A chart shows up to 40 values.

![Data > Chart Column... on the Revenue column.](shot:sheet-chart){full}

**Chart Gallery...** chooses the chart type: **Area**, **Bar**,
**Column**, **Line**, **Pie**, **Scatter** or **Combination**. The chart
redraws at once. **Export Chart as BMP...** saves the chart as a picture
(`CHART.BMP` unless you type another name) that Paint and PiXEL can open.

To chart a saved file without Sheet, use the **Chart** program,
[[CHART.O88]].

## The Options menu

| Item | Key | What it does |
|---|---|---|
| Display... | | ticks for **Formulas**, which shows each formula in its cell instead of the result, and **Gridlines** |
| Freeze Panes | | keeps the rows above and the columns left of the selected cell in view while the rest scrolls. The item then reads **Unfreeze Panes** |
| Protect Document | | protects the sheet; see below. The item then reads **Unprotect Document** |
| Calculation... | | **Automatic** recalculates as you go. **Automatic except Tables** leaves Data > Table results until you choose Calculate Now. **Manual** stops recalculating, which keeps a large sheet quick. The dialog also has **Calculate Now** |
| Calculate Now | F9 | recalculates every formula |
| Workspace... | | a tick for **R1C1**, which shows references in the reference box as `R2C3` instead of `C2` |

Freeze Panes on cell A1 has nothing to freeze, and Sheet says `Select below
or right of the split first.` Each sheet keeps its own frozen panes.

### Protecting a sheet

Every cell starts **Locked**. That makes no difference until you choose
**Options > Protect Document**: then a locked cell cannot be changed, and
Sheet says `Locked cell on a protected document.` To leave some cells open
for typing, select them first and choose **Format > Cell Protection...**,
**Unlocked**. **Hidden** keeps a cell's formula out of the formula bar while
the document is protected. There is no password. Protect Document is not
saved with the file.

## Four sheets in one workbook

The **Sheets** menu lists **Sheet1** to **Sheet4**; the one you are looking
at is marked. Choose another to switch to it. A formula reaches another
sheet with a reference like `Sheet3!B2`.

Only the Normal file format keeps all four sheets. The other formats save the
sheet you are looking at, and when another sheet holds data the status line
says `Saved - THIS SHEET ONLY; use .BIF to keep them all.`

## Macros

A macro is a column of formulas that Sheet carries out one cell after the
other, from the top. It can sit on any sheet of the workbook. **File >
New...**, **Macro Sheet** starts a workbook that shows formulas instead of
their results, which suits one made of macros. To run a macro, choose
**Macro > Run** and type the cell it starts in, or its name.

Macro formulas use the worksheet functions and commands of their own. Some
of them:

| Function | What it does |
|---|---|
| `SELECT(ref)` | selects a cell or block |
| `FORMULA(text, ref)` | enters text into a cell as if you typed it |
| `SET.VALUE(ref, value)` | puts a value in a cell |
| `GOTO(ref)` | carries on from another cell |
| `IF(test, GOTO(ref))` | branches |
| `FOR(counter, start, end)`, `WHILE(test)`, `NEXT()`, `BREAK()` | loops |
| `ALERT(text)`, `INPUT(prompt)`, `MESSAGE(TRUE, text)`, `BEEP()` | talk to the user |
| `COPY()`, `CUT()`, `PASTE()`, `CLEAR()`, `CALCULATE.NOW()` | the menu commands |
| `RETURN()`, `HALT()` | end the macro |

There are more: subroutines with `ARGUMENT` and `RETURN(value)`, error
handling with `ERROR`, keys and timers with `ON.KEY` and `ON.TIME`, text
files with `FOPEN`, `FREADLN` and `FWRITELN`, information with `GET.CELL`
and `GET.DOCUMENT`, dialogs with `DIALOG.BOX`, and menus of your own with
`ADD.MENU` and `ADD.COMMAND`.

A macro ends at `RETURN()` or `HALT()`, at an empty cell, or after 10,000
steps, when it says `Err: macro step limit`. Press **Esc** while one runs to
get the Single Step dialog: **Step** runs the next cell, **Halt** stops, and
**Continue** carries on.

**The recorder** writes a macro for you. Select an empty cell where it
should start, for example on Sheet4, and choose **Macro > Set Recorder**. Then choose
**Macro > Record...**, give the macro a name, and work as usual: Sheet
records each cell you select and each entry you type, and Copy, Cut, Paste,
Clear and Calculate Now. Choose **Stop Recorder** to finish. Other commands
are not recorded.

| Item | What it does |
|---|---|
| Record... | asks for a name and starts recording. While recording, the item reads **Stop Recorder** |
| Run | asks for the cell or name a macro starts at, and runs it |
| Start Recorder | carries on recording at the end of the last recording. Greyed until a recorder cell is set, and while recording |
| Set Recorder | makes the selected cell the place recordings go |
| Relative Record | records cell moves relative to the cell before. The item then reads **Absolute Record** |

## The File menu

| Item | Key | What it does |
|---|---|---|
| New... | Ctrl+N | asks Worksheet, Chart or Macro Sheet. Each one empties the workbook, its names included, and starts one called `SHEET1.SLK` |
| Open... | Ctrl+O | opens a spreadsheet file |
| Save | Ctrl+S | saves under the current name |
| Save As... | | asks for the file format, then for the name |
| Delete... | | picks a file and deletes it, after asking `Delete this file?` |

If the workbook has changes that are not saved, New, Open and closing the
window first ask **Save changes to** the file. **Save** saves it under its
current name and carries on, **Discard** carries on without saving, and
**Cancel** leaves everything as it was. If the save could not keep
everything, for example because a SYLK file keeps only one sheet, Sheet stops
and says so on the status line instead of carrying on.

To quit, choose **Close** from the Sheet menu at the top of the screen, or
click the window's close box.

## Keys

| Key | What it does |
|---|---|
| Arrows | move one cell; while typing, Up and Down store the entry and move |
| Shift+arrows | extend the selection |
| Page Up, Page Down | move a screenful |
| Home | goes to column A |
| Enter, Tab | store the entry and move down or right |
| F2 | edits the selected cell |
| Esc | throws away the entry you are typing |
| Delete | empties the selected cell |

The other keys are shown beside their items in the menus.

## Files

Sheet picks the format by the file's extension.

| Format | Extension | Keeps |
|---|---|---|
| Normal | `.BIF` or `.XLS` | Excel's own format: values, labels, formulas, every number format, alignment, bold and underline, borders, cell protection, notes, names, data tables, column widths, row heights and all four sheets |
| SYLK | `.SLK` | values, labels, formulas, the General, currency, comma and percent formats, alignment, borders, notes, names and column widths, one sheet |
| DIF | `.DIF` | values and labels, one sheet |
| CSV | `.CSV` | values and labels separated by commas, one sheet |
| Text | `.TXT` | values and labels separated by tabs, one sheet |
| DBF 3 | `.DBF` | a dBASE III table: the first row is the field names, each row below a record, one sheet |

Save As starts on the format the current name has, so OK alone keeps the file
in its format. Choosing a format gives the name that format's extension,
and the extension the name has when you save is the format written. A file
with any other extension opens as SYLK. Saving recalculates every formula
first, so the file holds up-to-date results. These files also open in Excel
and in other spreadsheets that read these formats.

## Good to know

- A workbook holds about 1,600 filled cells across its four sheets.
  Formulas, labels and notes share about 8KB of text, and changing a formula
  or label uses more of it. When it runs out, Sheet refuses the edit; a sort
  stops with `Sort incomplete - text area full.` Save, close and reopen the
  file to get the room back.
- Sheet opens files up to 32KB. A workbook larger than a format can save is
  cut short, and the status line says `Saved - TRUNCATED; sheet too large
  for this format.`
- A cell entry can be up to 63 characters long.
- Sort carries up to 512 rows.
- A workbook too large for Undo's memory cannot be undone, and Undo reads
  `Can't Undo`.
- An empty cell counts as 0 in a formula.
- Without CHART.OVL beside SHEET.O88, you can still type and calculate, but
  Sheet cannot open or save files, open its dialogs or draw charts, and says
  `CHART.OVL not found.` The text, logarithm and trigonometry, finance,
  database, information and matrix functions then show `#VALUE!`. Without
  MACRO.OVL, macros do not run.
- Each dialog can be closed with its Cancel button. The one-line dialogs,
  such as Goto, Find and Column Width, also take **Enter** for OK and
  **Esc** for Cancel.
- **Help > About Sheet...** and **Sheet > About Sheet** show who made it.
