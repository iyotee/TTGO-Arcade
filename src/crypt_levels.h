// Crypt: the ten depths. Hand-made maps, one string per level (rows of equal
// width, at most 32 x 32). Included by crypt.cpp only.
//
//   #  stone      M  mossy brick   C  coral      R  rune wall
//   D  door       G  gold door     B  blue door  X  exit arch (walk into it)
//   @  start      z  drowner       j  gloomjelly a  lurker     W  the Warden
//   h  tonic (+health)  m  pearl (+mana)  g / b  gold / blue key
//   $  treasure   u  wand upgrade
//   t  brazier (light)  p  pillar  o  barrel  k  kelp  x  bones
#pragma once
#include <Arduino.h>

namespace cryptgame {

struct CrLevel {
  const char *name;
  const char *hint;
  const char *map;
  uint8_t w, h;
  uint8_t floorTex;
  uint8_t fogR, fogG, fogB;
  uint8_t fog10;          // fog shade steps per cell x 10
  uint16_t par;           // seconds; faster exits earn a time bonus
};

static const char CR_MAP1[] PROGMEM =
  "################"
  "#@..#....t.....#"
  "#...D..........#"
  "#.h.#...z...m..#"
  "#####..........#"
  "#...#####D######"
  "#.t.#....z....k#"
  "#...D.........$#"
  "#...#..t...k...#"
  "#.m.#####D######"
  "#...#.....z....#"
  "#.z.D....m...t.X"
  "#..t#..h.......#"
  "################";

static const char CR_MAP2[] PROGMEM =
  "##################"
  "#@...#.....#.....#"
  "#....D..j..D..z..#"
  "#.t..#.....#..t..#"
  "#....###D###.....#"
  "##D###.....###G###"
  "#....#..z..#.....#"
  "#.m..#.....#..$..#"
  "#....D..t..#.....#"
  "#.z..#.....#..h..X"
  "#....####D##..j..#"
  "#..g.#.....#.....#"
  "#....#..h..#..z..#"
  "#.k..D..z..#..t..#"
  "#..t.#.....#..m..#"
  "##################";

static const char CR_MAP3[] PROGMEM =
  "####################"
  "#@..#..k.....k..#..#"
  "#...D...........D.h#"
  "#.t.#..p..p..p..#..#"
  "#...#...........#..#"
  "##D##..p..j..p..##G#"
  "#...#...........#..#"
  "#.m.#..p..p..p..#.j#"
  "#...D..k..z..k..#..#"
  "#.z.#MMMMDMMMMMM#..#"
  "#...M.......M...#.$#"
  "#.g.M..u..j.M.t.#..#"
  "#...D.......D...G..X"
  "#.t.M..z..t.M.m.#..#"
  "#...MMMMMMMMM.z.#.h#"
  "####################";

static const char CR_MAP4[] PROGMEM =
  "####################"
  "#@..#..x...#...x..j#"
  "#...D..z...D.......#"
  "#.t.#....t.#..z..h.#"
  "#...####D########B##"
  "#.m.#.........#....#"
  "#...#..x.j..x.#..g.#"
  "##D##..p...p..#....#"
  "#...D....t....#.z..#"
  "#.z.#..p...p..#..t.#"
  "#...#..x.z..x.#.$..#"
  "######D###G#########"
  "#..b....#.....z...m#"
  "#.x..z..#..t.......#"
  "#.......#...x...j..X"
  "#.t..m..#......t...#"
  "#....z..#..z....h..#"
  "####################";

static const char CR_MAP5[] PROGMEM =
  "CCCCCCCCCCCCCCCCCCCCCC"
  "C@..C.....k.....C....C"
  "C...D..a........D.j..C"
  "C.t.C..p..p..p..C....C"
  "C...C...........C..h.C"
  "CCDCC..p..t..p..CCDCCC"
  "C...C...........C....C"
  "C.m.D..p..p..p..C..g.C"
  "C...C.....z.....C.z..C"
  "CCCCCCCCCDCCCCCCCCCCCC"
  "C.....C.......C......C"
  "C..z..D...j...D...$..C"
  "C.k...C.......C......C"
  "C..h..C..t.t..C...z..C"
  "CCCDCCCC..a..CCCCCCCCC"
  "C.......C...C.....m..C"
  "C..t..j.D...G..a..h..X"
  "CCCCCCCCCCCCCCCCCCCCCC";

static const char CR_MAP6[] PROGMEM =
  "######################"
  "#@..#.....j.....#....#"
  "#...D.RRRR.RRRR.D..u.#"
  "#.t.#...........#.z..#"
  "#...#.RRRR.RRRR.#..m.#"
  "##D##...z...t...##B###"
  "#...#.RRRR.RRRR.#....#"
  "#.h.#.....a.....#.g..#"
  "#...D.RRRR.RRRR.#..j.#"
  "#.b.#...........#....#"
  "#...###D###D#####..t.#"
  "#.z.#.....#.....#....#"
  "#...#..x..#.$k..#.a..#"
  "#.m.#..z..#..j..#....#"
  "##############G#######"
  "#.....#........#.....#"
  "#..R..D...x....D..j..#"
  "#.z...#..t..R..#.....X"
  "#..m..#...a....#..t..#"
  "######################";

static const char CR_MAP7[] PROGMEM =
  "MMMMMMMMMMMMMMMMMMMMMMMM"
  "M@..M...z..M....M..o..jM"
  "M...D......D....D......M"
  "M.t.M.o..o.M.z..M.o..h.M"
  "M...MMMMDMMMMDMMMMMDMMMM"
  "M.h.M......M......M....M"
  "MMDMM.z..j.M..a...M.z..M"
  "M...D......D......D....M"
  "M.m.M..t...M...t..M..b.M"
  "M...MMMMMMMMMMMMMMM....M"
  "M.z.M.o.o.o.M.....MMBMMM"
  "M...D.......D..z..M....M"
  "M.o.M.z...z.M..$..M.g..M"
  "M...MMMMDMMMMMMDMMM..a.M"
  "M.k.M......M......M....M"
  "M...D..a...D...j..MMGMMM"
  "M.t.M......M......M....M"
  "M...M.o..o.M.z..t.M..h.X"
  "M.m.M...h..M......M..z.M"
  "MMMMMMMMMMMMMMMMMMMMMMMM";

static const char CR_MAP8[] PROGMEM =
  "########################"
  "#@.....k......k.......j#"
  "#.####.########.######.#"
  "#.#t.#.#..z...#.#..h.#.#"
  "#.#..D.#.####.#.D..m.#.#"
  "#.#..#.#.#..#.#.#....#.#"
  "#.####.#.#.b#.#.######.#"
  "#..z...#.#..#.#....a...#"
  "######.#.##D#.##########"
  "#....#.#......#....#...#"
  "#.h..D.###DD###.$..B.z.#"
  "#....#..k.......k..#...#"
  "####################.###"
  "#..........z...........#"
  "#.##########.#########.#"
  "#.#...t....#.#..j....#.#"
  "#.D..a...g.#.#....u..D.#"
  "#.#...m....#.#.t.....#.#"
  "#.##########.##G######.#"
  "#..j..................z#"
  "#############G##########"
  "#.k..t...a...D.....h...X"
  "########################";

static const char CR_MAP9[] PROGMEM =
  "CCCCCCCCCCCCCCCCCCCCCCCC"
  "C@..C......C.....C....$C"
  "C...D..a...D..z..D..j..C"
  "C.t.C..o...C.....C.o...C"
  "C...CCCCDCCCCCDCCCCCDCCC"
  "CCDCC..j...C..t..C.....C"
  "C...C......C.....C..a..C"
  "C.m.D..z...B..$..D.....C"
  "C...C....h.C.....C.z.m.C"
  "C.z.CCCCDCCCCCCCCCCCDCCC"
  "C...C.....C...a....C...C"
  "C.h.D..t..D........D.g.C"
  "C...C.....C..p..p..C...C"
  "CCDCCCCDCCC........CCDCC"
  "C......C..C..p..p..C...C"
  "C.a.$..C..D........D.$.C"
  "C......C..C...j....C...C"
  "C..b...C..CCCCDCCCCC.a.C"
  "C...o..D......z......D.C"
  "C.k....C.$...t...k...C.C"
  "CCCCGCCCCCCCCCCCCCCCCCCC"
  "C..z...j...a....m..h.D.C"
  "C.t...........t......C.X"
  "CCCCCCCCCCCCCCCCCCCCCCCC";

static const char CR_MAP10[] PROGMEM =
  "RRRRRRRRRRRRRRRRRRRRRRRR"
  "R@..R....j.....R...h..aR"
  "R...D..........D.......R"
  "R.t.R..p.....p.R..t....R"
  "R...R....z.....RRRRBRRRR"
  "RRDRR..p..t..p.R.......R"
  "R...R..........R..g.$..R"
  "R.h.RRRRDRRRRRRR...a...R"
  "R...R....a.....R.......R"
  "R.m.D..........RRRRRRRRR"
  "R...R.o..o..o..R....j..R"
  "R.z.R....h.....D.......R"
  "R...RRRRRRRRDRRR..m..t.R"
  "RRDRR..........R.......R"
  "R...R..a....j..RRRRRRRRR"
  "R.b.D..........R....h..R"
  "R...R...t..t...G.......R"
  "R.t.RRRRRRRRRRRR...z...R"
  "RRRRRRRRRRRRRRRRRRRRRDRR"
  "R..p..t....t....t..p...R"
  "R......................R"
  "R.p.......W.........p..X"
  "R....t.....h....t......R"
  "RRRRRRRRRRRRRRRRRRRRRRRR";

static const CrLevel CR_LEVELS[] = {
  {"The Flooded Gate", "Find the glowing arch.", CR_MAP1, 16, 14, TX_FLOOR, 8, 22, 30, 15, 90},
  {"Hall of Barnacles", "Gold doors need the gold key.", CR_MAP2, 18, 16, TX_FLOOR, 8, 20, 32, 16, 120},
  {"Kelp Cloisters", "A new wand rests in the cloister.", CR_MAP3, 20, 16, TX_SAND, 6, 24, 22, 16, 150},
  {"The Ossuary Tide", "Blue key first, then the gold.", CR_MAP4, 20, 18, TX_FLOOR, 10, 14, 24, 17, 170},
  {"Coral Chapel", "Lurkers bite. Keep your distance.", CR_MAP5, 22, 18, TX_SAND, 24, 10, 30, 16, 170},
  {"Sunken Library", "The runes remember the way.", CR_MAP6, 22, 20, TX_FLOOR, 6, 12, 34, 18, 200},
  {"Drowned Barracks", "The garrison never left.", CR_MAP7, 24, 20, TX_FLOOR, 14, 18, 14, 17, 220},
  {"Abyssal Aqueduct", "Follow the water down.", CR_MAP8, 24, 23, TX_SAND, 4, 18, 34, 18, 240},
  {"Black Pearl Vault", "Riches, and those who guard them.", CR_MAP9, 24, 24, TX_SAND, 20, 8, 26, 18, 260},
  {"Throne of the Tide", "The Warden waits beyond.", CR_MAP10, 24, 24, TX_FLOOR, 24, 4, 10, 17, 280},
};
static const int CR_NUM_LEVELS = sizeof CR_LEVELS / sizeof CR_LEVELS[0];

}  // namespace cryptgame
