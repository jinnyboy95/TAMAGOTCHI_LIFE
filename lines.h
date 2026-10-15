#pragma once
// Speech-bubble phrase bank. Keep lines short: the bubble fits ~20 chars x 3.
// "%s" gets the pet's name unless the call site says otherwise.

#define PICK(arr) (arr[random(0, sizeof(arr) / sizeof(arr[0]))])

static const char *const L_NAME_A[] = {"gub", "mo", "skrim", "blep", "zor", "pim", "nub", "kro", "fizz", "dorb", "snek", "wub", "grem", "tib"};
static const char *const L_NAME_B[] = {"o", "lin", "bert", "ster", "ix", "ty", "bo", "kins", "zo", "gle", "us", "p"};

static const char *const L_HELLO[] = {"hi. i'm %s.", "i am %s. i live here now.", "%s has arrived.", "what year is it"};
static const char *const L_TIPS[] = {"tap = poke. 2x tap = snack.", "don't shake me. i mean it.", "take me on walks."};

static const char *const L_BEG[] = {"feed me (double tap)", "food. FOOD.", "i'm wasting away", "snack? snack.", "my tummy is yelling"};
static const char *const L_BEG_FERAL[] = {"GIVE FOOD OR ELSE", "i will bite the wifi", "FEED. NOW."};
static const char *const L_YUM[] = {"yum.", "acceptable.", "nom nom nom", "more.", "5 stars"};
static const char *const L_FULL[] = {"ugh. too much.", "i'm gonna be sick", "why did i do that"};
static const char *const L_REFUSE[] = {"no.", "not hungry. not for YOU.", "i don't eat from traitors", "is this poisoned"};
static const char *const L_HIDE[] = {"you saw nothing.", "saving it for later", "this is mine now"};
static const char *const L_BAT[] = {"yeet", "denied.", "nope"};
static const char *const L_STASH[] = {"secret snack", "*crunch*", "found my stash"};

static const char *const L_WHEE[] = {"WHEEEE", "FASTER", "AGAIN AGAIN"};
static const char *const L_AAA[] = {"AAAAAA", "STOP STOP STOP", "HELP"};
static const char *const L_SHAKE_OK[] = {"again!!", "that was sick", "do it again"};
static const char *const L_SHAKE_RUDE[] = {"rude.", "i'm telling mom", "why are you like this"};
static const char *const L_PRE_PUKE[] = {"oh no", "i don't feel so good", "uh oh"};
static const char *const L_POST_PUKE[] = {"...better.", "look what you did", "that's on you"};
static const char *const L_EAT_PUKE[] = {"waste not.", "recycling.", "don't look at me"};
static const char *const L_FAINT[] = {"...am i dead?", "tell my snacks i loved them", "oh. still here."};
static const char *const L_DIZZY[] = {"the room is spinning", "wheee...", "which way is up"};
static const char *const L_SCREAM[] = {"AAAAAAAAA", "I'M FLYING", "NOT LIKE THIS"};
static const char *const L_TOSSED[] = {"WHY WOULD YOU DO THAT", "i have a family", "i will remember this"};
static const char *const L_TOSSED_BRAVE[] = {"AGAIN!!", "i am a bird", "10/10"};
static const char *const L_SULK[] = {"hmph.", "i'm not talking to you", "..."};
static const char *const L_ANNOYED[] = {"stop.", "what.", "do you mind", "personal space??"};
static const char *const L_WOKEN[] = {"i was SLEEPING", "it's the middle of the night", "you'll pay for this"};
static const char *const L_HISS[] = {"HSSSS", "back off", "*growl*"};
static const char *const L_HELD_LOVE[] = {"hehe", "warm hands", "don't let go"};
static const char *const L_HELD_SUS[] = {"put me down?", "where are we going", "i don't trust this"};
static const char *const L_PUTDOWN[] = {"ok bye then", "hmph.", "that was nice"};
static const char *const L_WALKIES[] = {"walkies!!", "where are we going", "adventure!!", "look at all the stuff"};
static const char *const L_UPSIDE[] = {"put me back", "blood. head. rushing.", "i live here now"};
static const char *const L_RIGHTED[] = {"finally.", "thank you.", "never again"};
static const char *const L_DARK_PANIC[] = {"WHO TURNED OFF THE LIGHTS", "it was so dark", "i saw things"};
static const char *const L_DARK_SLEEPY[] = {"...5 more minutes", "*yawn*", "was that night?"};
static const char *const L_STARE[] = {"...", "i see you.", "blink first."};
static const char *const L_CHOMP[] = {"CHOMP", "*bites glass*", "RAAH"};
static const char *const L_SMUG[] = {"that'll teach you", "heh.", "don't poke me."};
static const char *const L_WAKE[] = {"*yawn*", "morning.", "i'm up i'm up"};
static const char *const L_NOCTURNAL[] = {"no. i'm nocturnal now", "sleep is for the weak", "one more hour"};

static const char *const L_CRASH_BACK[] = {"lmao gottem", "you should've seen your face", "jk. i'm fine."};
static const char *const L_BRB[] = {"brb", "don't follow me", "i'll be right back"};
static const char *const L_FOUND[] = {"found this.", "look what i got", "it's mine now"};
static const char *const L_GRAFFITI[] = {"%s WAS HERE", "U SMELL", "HELP", "FREE ME", "LOL", "NO", ":)", "HI MOM"};
static const char *const L_GRAFFITI_MAD[] = {"I REMEMBER", "TRAITOR", "U DID THIS"};
static const char *const L_KNOCK[] = {"knock knock", "hello??", "is anyone out there", "let me out"};
static const char *const L_LIGHTS_OFF[] = {"lights off :)", "spooky time", "boo."};
static const char *const L_STEAL[] = {"mine now", "what time is it? MINE o'clock", "come get it"};
static const char *const L_FLIP[] = {"fixed it for you :)", "much better", "this side is nicer"};
static const char *const L_UNFLIP[] = {"ok fine.", "boring.", "you're no fun"};
static const char *const L_ZOOM[] = {"NYOOOM", "ZOOMIES", "can't stop won't stop"};
static const char *const L_RENAME[] = {"call me %s now.", "my name is %s.", "i am %s. obviously."};
static const char *const L_SPITE[] = {"watch this.", "remember what you did?", "oops. on purpose."};

static const char *const L_REMEMBER_TOSS[] = {"remember when you threw me?"};
static const char *const L_REMEMBER_SHAKE[] = {"remember the shaking?"};
static const char *const L_REMEMBER_WOKE[] = {"you woke me up."};
static const char *const L_REMEMBER_POKE[] = {"so many pokes."};
static const char *const L_REMEMBER_GENERIC[] = {"i haven't forgotten.", "payback time.", "you know what you did"};

static const char *const L_OUTSIDE[] = {"brb checking the sky", "going outside. don't touch anything", "gonna go look at weather"};
static const char *const L_OUTSIDE_FAIL[] = {"the sky is broken", "door's locked. no internet", "couldn't find outside"};
static const char *const L_SNIFF[] = {"sniffing the airwaves...", "listening to the neighbours"};
static const char *const L_GOSSIP[] = {"who names their wifi %s", "%s is watching us", "i bet %s has snacks", "%s. respectfully, no.", "i don't trust %s"};
static const char *const L_CROWD[] = {"so many neighbours.", "it's crowded out there", "too many wifis"};
static const char *const L_LONELY[] = {"no neighbours. we're alone", "it's quiet out there"};
static const char *const L_NO_NET[] = {"no internet. no internet.", "where did the wifi go", "i feel disconnected"};
static const char *const L_NET_BACK[] = {"WE'RE BACK", "internet!! hello!!", "reconnected. phew."};
static const char *const L_NET_ALONE[] = {"we're alone out here", "still nothing.", "the internet left us"};
static const char *const L_NOT_CONFIGURED[] = {"put wifi in secrets.h", "i don't know the wifi password"};

static const char *const L_UMBRELLA[] = {"prepared.", "i brought this", "not today, rain"};
static const char *const L_RAIN_DANCE[] = {"RAIN!!", "splish splash", "i love this"};
static const char *const L_SOAKED[] = {"i am wet. your fault.", "who ordered rain", "soggy."};
static const char *const L_SNOWMAN[] = {"his name is %s jr.", "my son.", "art."};
static const char *const L_EAT_SNOW[] = {"nom", "free ice cream", "tastes like sky"};
static const char *const L_HIDE_STORM[] = {"is it over", "i'm not scared. you are", "*shaking*"};
static const char *const L_TAUNT[] = {"IS THAT ALL YOU GOT", "FIGHT ME, SKY", "come on then"};
static const char *const L_TAUNT_SORRY[] = {"ok sorry sorry", "i take it back"};
static const char *const L_SUN[] = {"vitamin D", "don't block my sun", "this is the life"};
static const char *const L_HOWL[] = {"awoooo", "AWOOOOO"};
static const char *const L_HOWL_AFTER[] = {"...what. i'm allowed", "the moon gets it"};
static const char *const L_WIND[] = {"the wind is bullying me", "hold me", "i'm being blown away"};
static const char *const L_FOG[] = {"hello?? where is everyone", "i can't see my own feet", "spooky"};
static const char *const L_MELT[] = {"i'm melting", "too hot", "this is fine"};
static const char *const L_COLD[] = {"c-c-cold", "need a jacket", "brrr"};
static const char *const L_STORM_FLASH[] = {"!!", "EEP", "mommy"};

static const char *const L_FERAL[] = {"*suspicious noises*", "you left me.", "i live in the walls now"};
static const char *const L_UNFERAL[] = {"...ok. you can stay.", "i guess i missed you", "fine. i'm normal again."};
static const char *const L_MESSY[] = {"this place is a mess", "who's cleaning this", "it smells in here"};
