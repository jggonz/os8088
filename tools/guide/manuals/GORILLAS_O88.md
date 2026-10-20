# Gorillas

Gorillas is the banana-throwing game from QBasic, made for os8088. Two
gorillas stand on the rooftops of a city skyline and take turns throwing
exploding bananas at each other. You set the angle and the speed of each
throw, and the wind and gravity bend its path. Play against the computer or
against a friend at the same keyboard.

![Kong's turn to throw, with angle 62 and velocity 58 typed in.](shot:gorillas-aim){full}

## Starting Gorillas

![The opening screen.](shot:gorillas-intro){left}

Gorillas is at the top level of the Games disk, and in the `GAMES` folder of
the larger software disks. Double-click **GORILLAS.O88**.

The game opens with its title tune and a marquee of lights around the two
gorillas. Press any key to go straight to the setup, or let the tune finish.

## Setting up a match

![Setup asking for the gravity.](shot:gorillas-setup){right}

The setup asks seven questions, one at a time. Type an answer and press
`Enter`, or press `Enter` alone to take the default. `Backspace` deletes a
character.

1. **Number of players**: `1` or `2` (default 2). With 1 player, the computer
   throws for the second gorilla.
2. **Name of Player 1** and **Player 2**: up to 10 characters each. Blank
   names become Player 1 and Player 2, or Computer in a one-player game.
3. **First to how many points?**: 1 to 99 (default 3).
4. **Gravity**: 0.001 to 9999.999, with up to three decimal places (default
   9.8, the Earth's). Lower gravity gives longer, higher throws.
5. **Gameplay music?**: Yes or No (default Yes). `Y` and `N` work too.
6. **Show aim guides?**: Yes or No (default No). With Yes, a line of dots
   leaves each gorilla's hand at the angle being aimed, the computer's
   included. It shows the angle only: the dots thin out and stop well
   before the banana would land.

Finally press `V` to watch the gorillas dance to the intro music, or `P` or
`Enter` to start playing. Any key ends the dance.

## Throwing

On your turn the top of the screen shows your name, both scores, your
**Angle** and **Velocity**, and the wind. The arrow under the city points the
way the wind blows, and a longer arrow means a stronger wind.

1. Type the angle and press `Enter`. The angle runs from 0 to 360 degrees,
   measured up from the ground towards the other gorilla. Above 180 aims
   downwards.
2. Type the velocity, 0 to 360, and press `Enter` to throw.

![A banana in flight. The text stays hidden until it lands.](shot:gorillas-flight){left}

The first digit you type replaces the old value. `Backspace` removes a digit,
`Tab` switches between the two fields, and the arrow keys change the
selected value by one (`Up` and `Right` raise it, `Down` and `Left` lower
it). Each player's last angle and velocity are kept for the next turn.

A banana that hits a building blows a crater in it, and the crater stays. A
banana can also come back down on the gorilla that threw it. Whoever is hit,
the other player scores the point.

## Scoring

![A direct hit. The gorilla on the left is gone in the blast.](shot:gorillas-point){right}

After a hit, the gorilla that scored does a victory dance. Then a new
skyline starts on its own after a short pause, or press `Enter` to go on
sooner. The players take turns to throw first on each new skyline.

The first player to reach the winning score wins the match. A final
scorecard lists both scores; press any key to set up another match.

## Full screen

Press `F` or `Alt+Enter`, or choose **Game > Full Screen**, to play on the
whole screen. Press `Esc`, `F` or `Alt+Enter` to go back to the window. The
throw and the city are kept.

![Full screen on a VGA card, in the original game's colours.](shot:gorillas-full){full}

On VGA, full screen uses the colours of the original game. On CGA it uses a
four-colour picture, where the window is black and white. Hercules is black
and white in both.

## Menus and keys

| Menu item | Key | What it does |
|---|---|---|
| Game > New Match | `N` | Goes back to the match setup |
| Game > Full Screen | `F` or `Alt+Enter` | Plays on the whole screen, or goes back to the window |
| Game > Pause | `P` | Pauses the game. `P` or `Enter` carries on |
| Gorillas > About Gorillas | | Shows the credits. Click or press a key to put them away |
| Gorillas > Close | | Closes the game |

| Key | What it does |
|---|---|
| `M` | Turns the gameplay music on or off, at any time. Sound effects stay on |
| `Enter` | Moves from angle to velocity, throws, or skips the pause after a point |
| `Esc` | Leaves full screen |

## Good to know

- The computer does not know where you are. Its first throw on a skyline is
  a guess, and after each throw it only sees whether the banana fell short
  or flew past you, so it takes a few throws to find you. A new skyline
  makes it start again. Buildings get in its way too.
- The game waits while its window is covered or while you work in another
  program.
- With an AdLib or Sound Blaster card set up in the Control Panel, three
  music tracks take turns, one per skyline. Without one, the PC speaker
  plays the original game's tunes.
- Nothing is saved: each match starts fresh.
