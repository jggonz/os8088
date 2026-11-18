# Byteopoly — player guide

Collect vintage computers, trade them and outlast your friends: two to four
players on one PC, or on two PCs joined by a null-modem cable.

This is the reader's guide. The binding rules and every number below are
SPEC.md §131; where this guide and the SPEC disagree, the SPEC is right and
this guide is the bug.

## Getting it

Byteopoly is `BYTEOPLY.O88`. It ships on its own disk, in all four
geometries: `byteopoly.img` (1.44MB), `byteopoly720.img`, `byteopoly120.img`
(1.2MB 5.25") and `byteopoly360.img` (360KB, the XT's drive). It is also on
the live USB/CD. Put the disk in B:, open it in the Locator and
double-click **BYTEOPLY.O88**. Two Byteopoly windows can run at once and
share nothing.

It runs on VGA, EGA, CGA and Hercules, on the 4.77 MHz XT with 640KB.

## Starting a game

The New Game screen has three tabs: **Same PC**, **Host Cable** and **Join
Cable**.

- **Players.** 2 to 4, added and removed with `-` and `+`. Click a name
  (or press Enter on it) to edit it: eight characters at most. Enter keeps
  the name, Esc cancels the edit. Each player picks a token, Floppy, Mouse,
  Joystick or Cassette, with Left/Right or a click. Two players cannot share
  a token.
- **Rules.** **Classic** ends when one player is left. **Quick Match** ends
  after 10, 20 or 30 rounds, and the richest player wins (below).
- **Start Game** (or `S`) begins. Everyone rolls for who goes first; ties
  roll again.

The keyboard works everywhere: Tab and the arrows move the focus, Enter or
Space presses, Esc goes back.

## The board

Twenty-eight spaces run clockwise around the screen. **BOOT** is
bottom-left, **REPAIR SHOP** top-left, **SWAP MEET** top-right and
**CRASH!** bottom-right.

- **Computers.** Sixteen, in six collections: Commodore (C), Home Micros
  (H), Apple (A), IBM (I), 16-bit Rivals (R) and Power PCs (P). Each cell
  shows the collection letter, so the colour is never the only clue on a
  monochrome screen.
- **Utilities.** **POWER** and **BBS**.
- **Event spaces.** **Floppy Find** and **System Error**, two of each, draw
  cards.
- **Charges.** **Import Duty** costs 100 cr and the **E-waste Fee** costs
  75 cr.

The sidebar lists the players with their cash. The current player's row is
inverted. Below that are the details of the selected space, the link
status for cable games, and the log of what just happened. Click a space,
or move to it with the arrows, to read its full rent table. Home jumps to
the current player.

## A turn

1. **Roll** with Space. You move clockwise. Passing or landing on BOOT pays
   200 cr.
2. **Land.**
   - **An unowned computer or utility.** **Buy** it (`B`), or send it to
     **Auction** (`A`).
   - **Someone else's.** You pay them rent.
   - **An event space.** Read the card and press Enter.
3. **Manage.** Before you end your turn you may trade (`T`), or open your
   collection (`M`) to upgrade, sell, mortgage or redeem.
4. **End Turn** (`E`). If you rolled doubles you must roll again first.
   Three doubles in a row send you to the Repair Shop.

A button you cannot press is grey, and pressing it says why. The bottom bar
always lists the keys the current panel accepts.

## Money

Everyone starts with 1,500 cr. Rent goes to the owner. Charges, cards,
purchases and auctions pay the bank.

- **Collections.** Owning a whole collection doubles the rent of its
  un-upgraded computers, as long as none of them is mortgaged.
- **Upgrades.** On a complete collection, add 1 to 4 RAM chips per
  computer, then a Workshop as the fifth step. Build and sell evenly: no
  computer in the set may be more than one step ahead of another. Rent at
  levels 1 to 5 is 5, 15, 30, 45 and 60 times the base rent. A step costs
  the collection's upgrade price, and selling one returns half.
- **Utilities.** Rent is 4 times the dice for one, 10 times for both.
- **Mortgages.** A mortgage pays half the price; the property earns no rent
  until you redeem it for the loan plus 10%. A computer can only be
  mortgaged when its whole collection has no upgrades.

## Auctions

Declining a purchase starts an auction among every player still in the
game, including you. Bids are multiples of 10 cr, each at least 10 above
the high bid, and never more than your cash. Bid with `B` (adjust the
amount with `-`/`+` or by typing it) or **Pass** with `P`; a pass is final.
A player who cannot raise is passed automatically, and the panel says so.
The last bidder standing pays and takes the computer. There is no timer.

## Trades

On your turn, between rolls, propose a trade (`T`) with one other player.
You can include any of either side's computers and utilities, their
Recovery Disks, and one cash amount in either direction. A collection with
upgrades cannot be traded. The offer is frozen once sent. Only the other
player can accept or reject it, and you can cancel it. If anything has
changed by the time they accept, the trade is refused as a whole; it never
half-happens.

## The Repair Shop

Landing on the Repair Shop is just a visit. CRASH!, the General Protection
Fault card or a third double in a row sends you there detained, with no
salary. To leave, do one of these at the start of your turn:

- pay 50 cr (`P`);
- use a Recovery Disk (`F` or `S`, for the deck it came from);
- roll doubles (`R`).

The third failed roll costs 50 cr, and then you move. While detained you
still collect rent, bid, trade and manage.

## Cards

Two decks of eight. **Floppy Find** is mostly good news, and **System
Error** mostly bad: Bench Service charges 25 cr per RAM chip and 100 cr per
Workshop you own. A **Recovery Disk** card is kept until you use or trade
it.

## Debt and bankruptcy

If you owe more than you have, the **Debt** panel shows the creditor, the
amount and your shortfall. Raise the cash by selling upgrades and
mortgaging, then **Settle** (`S`), or declare **Bankrupt** (`K`, then
confirm).

- **Bankrupt to a player.** That player gets everything you have left,
  mortgages and all.
- **Bankrupt to the bank.** The bank takes it back clean, for later players
  to buy.

Either way you are out of the game.

## Winning

- **Classic.** The last player standing wins.
- **Quick Match.** When the last round finishes, the game scores everyone:
  cash, plus the price of each property less any mortgage, plus the resale
  value of its upgrades. The highest score wins, and ties share the win.

The end screen offers **Review** (look at the final board), **New Game** and
**Exit**.

## Saving

**Game > Save** (F2) writes the match to a file, and **Game > Load** (F3)
reads one back. A damaged or foreign file is refused, and the running game
is left alone. Closing a game with unsaved moves asks whether to save
first.

## Two PCs over a null-modem cable

1. Join the two PCs' serial ports with a null-modem cable. Both run at 2400
   baud, 8N1; `Auto` finds the port, or pick COM1 or COM2.
2. On one PC choose **Host Cable** and press **Open Link**. On the other
   choose **Join Cable** and **Open Link**. The link box shows searching,
   then the peer, then whether the two copies of Byteopoly are compatible.
3. The host sets up the players and rules. The **Computer** column says
   which PC plays each seat. Any split works: 1+1, 1+2, 2+1, 2+2, 1+3 or
   3+1.
4. Both press **Ready**, then the host presses **Start Game**.

The host is the banker: it rolls the dice and decides every move. The other
PC replays each decision and checks it reaches the same game. You can only
act for the seats played on your PC. A panel for a seat on the other PC
shows its facts and says who it is waiting for.

- **Unplugged cable.** Both PCs pause with a **Link Paused** panel and
  nothing changes. Plug it back in and the game resumes exactly where it
  stopped. A move that was in flight happens once, never twice.
- **Paired Save.** **Game > Paired Save** saves the match on both PCs at
  the same moment.
- **Resuming.** Later, open the link again on both PCs and use **Game >
  Resume Pair** in the lobby. The two saves must match.
- **Chat.** **Help > Chat** sends a short line to the other PC.
- **Diagnostics.** **Options > Diagnostics** shows the link's frame
  counters.
- **Out of step.** If the two PCs ever disagree about the game, the joining
  PC offers **Use host**, which adopts the host's copy.

## Keys

| key | does |
|---|---|
| Space | roll |
| B | buy, or bid in an auction |
| A | send to auction, or accept a trade |
| P | pass a bid, or pay to leave the Repair Shop |
| R | roll for doubles in the Repair Shop, or reject a trade |
| T | propose a trade |
| M | your collection: upgrade, sell, mortgage, redeem |
| E | end your turn |
| S | settle a debt |
| K | declare bankruptcy |
| Tab / Enter | move the focus / press the focused button |
| Arrows / Home | move around the board / jump to the current player |
| F1 / F2 / F3 | rules / save / load |
| Esc | back: close a panel, cancel an edit |

## Sound

Byteopoly has a soundtrack and sound effects. With an AdLib or a Sound
Blaster and the system's sound driver loaded, the music is three FM voices:
a title theme on the New Game screen, a quiet loop while you play, a tenser
one while an auction is open, and a fanfare for the winner. With the PC
speaker only, the music plays its tune alone and starts **off** - turn it on
if you like it. The effects - dice, a step, buying, rent, BOOT's salary,
each kind of card, CRASH!, upgrades, mortgages, bids, the gavel, trades,
your turn, the cable dropping and coming back, bankruptcy and the win - play
on either, over the music.

**Options > Sound** turns the effects off and on, **Options > Music** the
music. The songs and effects ride the game's second art file, so on a
machine too short of memory to load it the game is silent. An idle game
draws nothing.
