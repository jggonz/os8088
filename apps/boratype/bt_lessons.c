/* Authored curriculum. Every drill is checked by bt_validate_lessons and the
 * host gate against its own declaration, not a permissive global alphabet. */
#define BT_HOME "asdfghjkl; "
#define BT_TOP "qwertyuiopasdfghjkl; "
#define BT_LOWER "abcdefghijklmnopqrstuvwxyz ,./;"
#define BT_CAPS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ,./;"
#define BT_DIGITS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ,./;"
#define BT_PUNCT "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ,./;:'\"?!"
#define BT_ALL " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
const BTLesson bt_lessons[BT_LESSONS] = {
 {"01 Find F and J", "fj ", "fj", "Rest index fingers on F and J. Feel their bumps; keep eyes here.",
  "f j f j ff jj fj jf fjj jff fj fj jf jf f j fj jf"},
 {"02 Add D and K", "dfjk ", "dk", "Middle fingers use D and K. Return index fingers to F and J.",
  "d k d k df jk fd kj dd kk dfjk kjfd fj dk df jk ddf jkk"},
 {"03 Add S and L", "sdfjkl ", "sl", "Ring fingers use S and L. Keep your wrists relaxed.",
  "s l ss ll sd kl ds lk sdf jkl lkj fds sf jl sl df jk sdfjkl"},
 {"04 Add A and semicolon", "asdfjkl; ", "a;", "Little fingers use A and semicolon. Thumbs share Space.",
  "a ; aa ;; as l; sa ;l asdf jkl; ;lkj fdsa sad lad fall ask flask"},
 {"05 Reach G and H", BT_HOME, "gh", "Index fingers reach G and H, then return to F and J.",
  "f g f j h j fg jh gh hg glad half hall had dash ash glass shall"},
 {"06 Home row words", BT_HOME, "", "Use all home keys. Accuracy matters more than speed; mastery is 95 percent.",
  "a sad lad had a flask; a glad lass had a glass; dad shall ask; all lads dash"},
 {"07 Index top reaches", "rutyasdfghjkl; ", "ruty", "Left index reaches R/T; right index reaches Y/U. Return home.",
  "frf juj rr uu fur rug rush rural aura guard gull lard hard sugar"},
 {"08 Reach E and I", "eirutyasdfghjkl; ", "ei", "Left middle finger reaches E. Right middle finger reaches I.",
  "ded kik ee ii ride fire hide guide field slide dear idea red user"},
 {"09 Reach W and O", "woeirutyasdfghjkl; ", "wo", "Left ring finger reaches W. Right ring finger reaches O.",
  "sws lol ww oo word wool wood slow show flower lower wide world"},
 {"10 Reach Q and P", BT_TOP, "qp", "Little fingers reach Q and P. Let the other fingers stay home.",
  "aqa ;p; qq pp quiet paper proper power equal quote proud square"},
 {"11 Top and home", BT_TOP, "", "Read ahead by a word; keep the home bumps as your anchors.",
  "we read quiet words; your old typewriter is proud; a square of paper waits"},
 {"12 Reach V and M", "qwertyuiopasdfghjkl;vm ", "vm", "Left index reaches V. Right index reaches M. Move one finger at a time.",
  "fvf jmj vv mm move movie vivid velvet volume mum marvel very merry"},
 {"13 Reach C and comma", "qwertyuiopasdfghjkl;vmc, ", "c,", "Left middle finger reaches C. Right middle finger reaches comma.",
  "dcd k,k cc ,, car, cream, comic, clear, clever, calm, crumple, comma"},
 {"14 Reach X and period", "qwertyuiopasdfghjkl;vmc,x. ", "x.", "Left ring finger reaches X. Right ring finger reaches period.",
  "sxs l.l xx .. six. mix. wax. extra. exact. a calm fox views a movie."},
 {"15 Reach Z and slash", "qwertyuiopasdfghjkl;vmc,x.z/ ", "z/", "Left little finger reaches Z. Right little finger reaches slash.",
  "aza ;/; zz // zero/zero jazz/fizz lazy/maze zigzag zoom azure razzle"},
 {"16 Reach B and N", BT_LOWER, "bn", "Index fingers reach B and N. All lowercase letters are now available.",
  "fbf jnj bb nn banana button bring brown. the quick brown fox jumps over the lazy dog."},
 {"17 Left Shift", BT_CAPS, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "Capitals on the right hand use left Shift. Release Shift after each capital.",
  "My name is Borat. I like July. You may join. Keep your fingers home. Never rush."},
 {"18 Right Shift", BT_CAPS, "", "Capitals on the left hand use right Shift. Caps Lock is optional for capitals.",
  "Ask Dad. Find Gary. Bring Red Flowers. Watch The Dog. Great Success. A Calm Start."},
 {"19 Digits 1 to 5", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ12345 ,./;", "12345", "Use left fingers for 1 to 5; return to the letter anchors after a number.",
  "1 2 3 4 5 12 23 34 45 51. Bring 5 pens, 4 pads, 3 cups, 2 bags and 1 hat."},
 {"20 Digits 6 to 0", BT_DIGITS, "67890", "Use right fingers for 6 to 0. Digits count toward speed like letters.",
  "6 7 8 9 0 67 78 89 90 06. Room 8088 has 640 pens. Type 1234567890 once more."},
 {"21 Sentence marks", BT_PUNCT, ":'\"?!", "Use opposite-hand Shift for colon, quotes, question mark and exclamation.",
  "\"Hello, friend!\" said Borat. It's 9:30; are you ready? Yes, I am. Don't rush!"},
 {"22 Shifted symbols", BT_ALL, "!@#$%^&*()_+-=[]{}\\|`~<>", "Find the printed base key; use Shift on the opposite hand for its upper symbol.",
  "1! 2@ 3# 4$ 5% 6^ 7& 8* 9( 0) -_ =+ [{ ]} \\| ;: '\" ,< .> /? `~ a+b=3"},
 {"23 Mixed sentences", BT_ALL, "", "Keep a steady rhythm. Wrong keys stay on the same target; Backspace retracts.",
  "My name is Borat. This keyboard has 101 keys, but today I need only a few. Accuracy first; speed will follow!"},
 {"24 Great success", BT_ALL, "", "A complete run at 95 percent masters this lesson. Repeat any lesson whenever you like.",
  "Great success! At 8:08, I typed \"Hello, world!\" with calm hands. Score 100%, keep F/J anchors, and try a 3-minute challenge."}
};

static int bt_contains(const char *s, int c)
{
    while (*s) { if ((BTByte)*s == c) return 1; s++; }
    return 0;
}

int bt_validate_lessons(void)
{
    int i, j, k;
    const BTLesson *l;
    const char *s;
    for (i = 0; i < BT_LESSONS; i++) {
        l = &bt_lessons[i];
        if (!l->name[0] || !l->instruction[0] || !l->drill[0]) return 0;
        s = l->allowed;
        for (j = 0; s[j]; j++) {
            if ((BTByte)s[j] < 32 || (BTByte)s[j] > 126) return 0;
            for (k = 0; k < j; k++) if (s[k] == s[j]) return 0;
            if (s[j] != ' ' && !bt_contains(l->introduced, s[j]) &&
                (!i || !bt_contains(bt_lessons[i - 1].allowed, s[j]))) return 0;
        }
        for (j = 0; l->introduced[j]; j++)
            if (!bt_contains(l->allowed, (BTByte)l->introduced[j])) return 0;
        for (j = 0; l->drill[j]; j++) {
            if (j == BT_PASSAGE || !bt_contains(l->allowed, (BTByte)l->drill[j])) return 0;
        }
    }
    return 1;
}
