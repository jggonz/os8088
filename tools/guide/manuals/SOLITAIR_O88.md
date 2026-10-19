# Solitaire

Solitaire is Klondike, the patience game most people know simply as
solitaire. Build the four suits up from ace to king on the foundations,
using the seven columns of the table to sort the cards out.

![A deal in progress, with a few moves already played.](shot:solitaire-window){full}

## Starting Solitaire

Open the software disk, then the `GAMES` folder, and double-click
**SOLITAIR.O88**. On the Games disk it sits at the top level instead. You
can open more than one Solitaire at a time; each deals its own game.

## The table

- **The stock**, top left, holds the cards still to be dealt, face down.
- **The waste**, beside it, is where dealt cards land, face up. Only its top
  card can be played.
- **The foundations**, top right, are four empty places marked with a faint
  spade, heart, diamond and club. Each takes one suit, from the ace up to the
  king.
- **The columns** below are dealt one to seven cards deep. Only the last card
  of each column starts face up.

## Moving cards

Drag a card and drop it where it should go. An outline follows the pointer
while you drag. If the move is not allowed, nothing changes.

![Dragging two cards: the outline shows where they are going.](shot:solitaire-drag){right}

- **On a column**, a card goes on a card one rank higher of the other
  colour: a red 6 on a black 7, a black jack on a red queen.
- **On an empty column**, only a king can go, with any cards on it.
- **On a foundation**, an ace starts the pile, then the 2, 3 and so on of the
  same suit. A foundation takes one card at a time.
- **A run of cards** can move together. Drag any face-up card in a column and
  every card below it comes with it.
- When you move the last face-up card off a column, the card under it turns
  face up by itself.
- Cards on a foundation can be dragged back down onto a column.

**A quick click** on a card, pressing and letting go without moving, sends
that card to its foundation if it fits there. It works on the waste's top
card and the last card of any column.

## Dealing

![Draw Three: the waste fans out three cards.](shot:solitaire-three){left}

Click the stock to deal. In **Draw One** a single card goes onto the waste;
in **Draw Three** three cards go over at once, fanned so you can see them,
and only the top one can be played. `Space` deals as well.

When the stock is empty it shows a ring. Click it to turn the whole waste
back over onto the stock and go through it again. You can do this as often
as you like.

## Winning

Put all 52 cards on the foundations and a **You win!** card appears on the
table. Choose **New Game** for another deal. **Auto Finish** helps at the
end: it sends every card that can go up to the foundations, one move at a
time, until nothing more fits.

## The Game menu

| Item | Key | What it does |
|---|---|---|
| New Game | `N` | Shuffles the cards and deals a new game |
| Restart Deal | `R` | Deals the same shuffle again from the start |
| Auto Finish | `A` | Moves every card that will go onto the foundations |

## The Deal menu

| Item | What it does |
|---|---|
| Draw One | Each click on the stock deals one card |
| Draw Three | Each click on the stock deals three cards |

![The Deal menu: Draw One is in force, so it is grey.](shot:solitaire-deal){right}

The mode in force is grey. A new Solitaire starts in Draw One, and a change
applies from the next deal without starting a new game.

**Solitaire > About Solitaire** shows who made it, and any click or key takes
the card down again. **Solitaire > Close** closes this game.

## Good to know

- Solitaire keeps no score and has no clock and no undo. It saves nothing to
  disk.
- The window takes its size from the screen. On a CGA screen the cards are
  smaller, and a long column squeezes its cards closer together so it still
  fits.
- On a black-and-white screen the hearts and diamonds are drawn as outlines
  and the spades and clubs solid, so you can still tell red from black.
