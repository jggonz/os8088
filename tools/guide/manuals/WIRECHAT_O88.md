# WireChat

WireChat is a chat program for os8088 users. It has four public rooms
(lobby, hardware, development and games), private conversations with one
other person, a list of who is in the room, a friends list, and a status
that tells others whether you are Online, Away or Busy. It talks to the
chat server at os8088.com.

![WireChat with a nick chosen and a message typed. With no network card, the status bar says so.](shot:wirechat-window){full}

## Starting WireChat

WireChat is **WIRECHAT.O88** in the `APPS` folder, and at the top of the
network disk. Double-click it to open it.

WireChat needs a network connection. Load the **Ethernet** driver
([[ETHER.DRV]]) for an NE1000 or NE2000 network card: open the **Control
Panel**, choose the **Drivers** page and tick **Ethernet**. The Browser's
manual, [[BROWSER.O88]], explains these steps in full. A DOS PC joined to
this machine by the **Parallel link** ([[NET.DRV]]) can carry the connection
instead, if that PC is on a network. Without a driver the status bar says
**No network card** and the line above the field says
`No network card - see Control Panel`. WireChat looks again every minute, so
it connects by itself once the driver is loaded.

## Choosing a nick

The first time WireChat opens, the field at the bottom says `Nick:` and asks
you to choose a nick, the name others see. A nick is 2 to 12 letters, digits,
`-` or `_`, and starts with a letter. Type it and press `Return`. WireChat
remembers it and uses it every time it opens.

If someone is already using that nick, or the server refuses it, the field
asks again with your nick in it to change. **Chat > Change Nick...** chooses
another at any time.

A few nicks belong to particular people. Choosing one of those asks for its
code: the field says `Code:` and the status bar says **Enter the code for
this nick**. Type the code and press `Return`. `Esc` goes back to choose a
different nick. WireChat remembers the code with the nick, as plain text on
the disk, so anyone with the disk has the code too.

## The window

| Part | What it shows |
|---|---|
| Header | Your nick, your status (**Offline** until connected) and the **Set status...** button |
| ROOMS | The four rooms, each with how many messages you have not read, and `@` when someone has mentioned you there |
| MESSAGES | Your private conversations, each with its unread count |
| FRIENDS ONLINE | Your friends who are connected now, and **+ Add friend** at the foot |
| Centre | The room's name and topic, the **Search** button, the messages, a line for notices, and the field with **:)** and **Send** |
| MEMBERS | Who is in this room, with each person's status. In a private conversation it is **ONLINE**, everyone connected |
| Status bar | The connection, how many are in the room, and **Notifications: On** or **Off** |

Each message shows its time and the sender's nick in bold, with the text
below. Times are local, worked out from the machine's clock, and a line such
as `---- Tue Oct 20 ----` above a message starts each new day, so you can
tell yesterday's messages from today's. The status glyphs are a filled
circle for Online, a clock for Away and a barred circle for Busy.

On a VGA or EGA screen WireChat is in colour. Unread counts are red badges,
and a room where you were mentioned has a magenta one. The room you are in
is blue. Each person's nick always has the same colour, and a message that
mentions you has a yellow header. Online is green, Away brown and Busy red.
Two lamps at the left of the status bar replace the link glyph. The first is
the connection: green when connected, amber while connecting, red with no
network card or when the server does not answer. The second lights amber
while a message you sent is on its way and for a moment when something
arrives. On a CGA or Hercules screen WireChat looks as it always did.

The window can be resized. A narrow window drops the MEMBERS pane, and a
narrower one drops the left pane too; the **Rooms** menu still changes room.
On a short screen such as a CGA, the topic line goes and each message takes
one line, `time nick: text`.

## Talking in a room

Type in the field and press `Return` or click **Send**. **Send** is greyed
while the field is empty. A message is up to 200 characters. The **:)**
button opens eight emoticons; click one to type it where the caret is.

Click a room in ROOMS, choose it from the **Rooms** menu, or press `Tab` for
the next room. The room's recent messages come with it.

To mention someone, type `@` and their nick: `@RetroAmy are you there?`.
Type `@` and the first letters of a nick and press `Tab` to finish it from
the people who are connected; press `Tab` again for the next nick that starts
the same way.

When messages arrive while another window is in front, WireChat marks where
you stopped reading with a line saying how many are new. The line goes when
you change room or send a message.

If a message is refused, the line above the field says why, and your text
stays in the field to change and send again.

## Private conversations

Click a nick in MEMBERS, MESSAGES or FRIENDS ONLINE to talk to that person
alone. The title shows `@` and the nick, and what you send goes only to
them. A conversation with unread messages shows a count in MESSAGES. Click a
room to go back to it.

## Status

![The Set status... popup.](shot:wirechat-status){right}

Click **Set status...** or use the **Status** menu to be **Online**, **Away**
or **Busy**. Ten minutes with no key and no click turns Online into Away by
itself, and the next key or click turns it back. A status you chose stays as
you set it.

## Friends

Click **+ Add friend** or choose **Friends > Add Friend...**, type the nick
after `Friend:` and press `Return`. Right-click a nick in MEMBERS to be asked
`Add <nick> as a friend?`. Friends who are connected appear under FRIENDS
ONLINE. Right-click a friend there to remove them, or open a conversation
with them and choose **Friends > Remove Friend**. WireChat keeps up to eight
friends.

## Searching

Click **Search**: the field says `Find:`. Type a word and press `Return` to
jump to the newest earlier message whose text or nick contains it, in
capitals or not. Press `Return` again for the one before that. **Not found**
means there are no more. `Esc` goes back to typing messages, with what you
were writing still there.

## Flagging a message

Click a message to select it, then choose **Chat > Flag Message...**, or
right-click the message. WireChat asks `Flag this message as abusive?`. A
room message that two people flag is removed for everyone. A private message
you flag is removed at once. You cannot flag your own messages.

## Notifications

With notifications on, WireChat chirps when a private message arrives, or
when someone in the room writes your nick, while another window is in front.
Click **Notifications:** in the status bar, or choose **Chat >
Notifications Off** or **Notifications On**, to switch.

When someone mentions you with `@` and your nick in a room you are not
looking at, WireChat chirps even if its window is in front. The line above
the field says who and where, for example `RetroAmy mentioned you in
#hardware`, and the room shows `@` in ROOMS until you go to it.

## The menus

| Menu | Item | What it does |
|---|---|---|
| Chat | Change Nick... | Choose a different nick |
| Chat | Flag Message... | Flag the selected message |
| Chat | Notifications Off / On | Switch the chirp off or on |
| Chat | Reconnect | Start a fresh connection to the server |
| Rooms | lobby, hardware, development, games | Go to that room |
| Status | Online, Away, Busy | Set your status |
| Friends | Add Friend... | Type a nick to add |
| Friends | Remove Friend | Remove the person in this conversation |

![The Chat menu.](shot:wirechat-menu-chat){left}

**WireChat > About WireChat** shows who made it; any key or click puts the
window back.

## Keys

| Key | What it does |
|---|---|
| `Return` | Send, or finish the nick, code, search or friend |
| `Esc` | Clear the field, or leave search and Add Friend |
| `Tab` | After `@` and part of a nick, finish the nick; otherwise the next room |
| `Up`, `Down` | Scroll the messages a line |
| `PgUp`, `PgDn` | Scroll the messages a page |

The scroll bar beside the messages scrolls them too, and so does the wheel
on a mouse that has one.

## The status bar

| It says | What it means |
|---|---|
| Choose a nick | No nick is chosen yet |
| No network card | No network driver is loaded |
| Connecting... | Joining the server |
| Connected | Talking to the server |
| Offline - retrying in 10s | The server did not answer; WireChat tries again by itself, waiting longer each time up to a minute |
| Resting - trying again later | The server is full or resting until midnight UTC; WireChat tries again by itself |
| Enter the code for this nick | The nick needs its code |

## Files

WireChat keeps its nick, the code, the friends list and the notification
switch in `WIRECHAT.CFG`, in `SYSTEM\APPDATA` on the disk it was started
from.

## Good to know

- The server decides what is allowed. It refuses messages and nicks with
  rude words, limits how fast one machine can send, and mutes a machine that
  keeps breaking the rules: ten minutes, then an hour, then a day.
- The server keeps only the last 30 messages of each room, and WireChat
  shows up to 40.
- WireChat sends one message at a time. Sending another before the first
  has gone says `Still sending the last message...`.
