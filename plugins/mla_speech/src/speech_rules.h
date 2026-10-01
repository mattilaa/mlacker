// Mla Speech - English text to phonemes.
//
// Letter-to-sound rules after the US Naval Research Laboratory report
// "Automatic Translation of English Text to Phonetics by Means of
// Letter-to-Sound Rules" (Elovitz, Johnson, McHugh, Shore; NRL Report 7948,
// 1976), the rule set the 1980s home-computer speech programs - the Atari ST's
// among them - were built on. Numbers are read out as words, words without
// vowels ("ST", "TV") are spelled, and text in [brackets] is taken as phoneme
// codes (`hEHlOW`). A little prosody follows: stressed first vowels, a falling
// pitch through each clause, a rise before "?", and pauses at punctuation.
//
// Everything works in fixed buffers: `translate` runs on the audio thread when
// a note brings its words, so it never allocates.

#pragma once

#include <cstdint>
#include <cstring>

namespace mla_speech {

// Phoneme codes shared with mla_speech_dsp.mla.
enum Phoneme : uint8_t {
    kPauseShort = 0, kPauseLong, kWordGap,
    kIY, kIH, kEY, kEH, kAE, kAA, kAO, kOW, kUH, kUW, kER, kAX, kAH, kAY, kAW, kOY,
    kP, kB, kT, kD, kK, kG, kF, kV, kTH, kDH, kS, kZ, kSH, kZH, kHH, kM, kN, kNG,
    kL, kW, kY, kR, kCH, kJH, kWH,
    kPhonemeCount
};

inline bool isVowelPhoneme(int code) { return code >= kIY && code <= kOY; }

// One utterance: phoneme codes with pitch and duration factors.
struct Utterance {
    static constexpr int kCapacity = 4096;
    uint8_t code[kCapacity];
    float pitch[kCapacity];
    float duration[kCapacity];
    int count = 0;
    bool full = false;
    void clear() { count = 0; full = false; }
    void add(int phoneme, float pitchFactor = 1.0f, float durationFactor = 1.0f)
    {
        if(count >= kCapacity) { full = true; return; }
        code[count] = static_cast<uint8_t>(phoneme);
        pitch[count] = pitchFactor;
        duration[count] = durationFactor;
        ++count;
    }
};

namespace detail {

// Phoneme spellings used by the rule outputs: two upper-case letters for
// vowels and digraphs, one lower-case letter for single consonants.
struct Spelling { const char *text; uint8_t code; };
constexpr Spelling kSpellings[] = {
    {"IY", kIY}, {"IH", kIH}, {"EY", kEY}, {"EH", kEH}, {"AE", kAE}, {"AA", kAA}, {"AO", kAO},
    {"OW", kOW}, {"UH", kUH}, {"UW", kUW}, {"ER", kER}, {"AX", kAX}, {"AH", kAH}, {"AY", kAY},
    {"AW", kAW}, {"OY", kOY}, {"TH", kTH}, {"DH", kDH}, {"SH", kSH}, {"ZH", kZH}, {"NG", kNG},
    {"CH", kCH}, {"WH", kWH},
    {"p", kP}, {"b", kB}, {"t", kT}, {"d", kD}, {"k", kK}, {"g", kG}, {"f", kF}, {"v", kV},
    {"s", kS}, {"z", kZ}, {"h", kHH}, {"m", kM}, {"n", kN}, {"l", kL}, {"w", kW}, {"y", kY},
    {"r", kR}, {"j", kJH},
};

// Parse one phoneme spelling at `text`; returns its length (0 if none).
inline int parsePhoneme(const char *text, int &code)
{
    for(const auto &spelling : kSpellings) {
        const size_t size = std::strlen(spelling.text);
        if(std::strncmp(text, spelling.text, size) == 0) {
            code = spelling.code;
            return static_cast<int>(size);
        }
    }
    return 0;
}

// Rule: left context, match, right context, output. "" is anything, " " a
// word boundary. Context symbols: # one or more vowels, : zero or more
// consonants, ^ one consonant, . a voiced consonant (B D V G J L M N R W Z),
// + a front vowel (E I Y), % a suffix (E ER ES ED ING ELY), @ a consonant
// that makes U sound like OO (T S R D L Z N J TH CH SH), & a sibilant.
struct Rule { const char *left, *match, *right, *out; };

#define N " "
#define A ""
constexpr Rule kRulesA[] = {
    {N, "ATARI", A, "AXtAArIY"}, {A, "A", N, "AX"}, {N, "ARE", N, "AAr"}, {N, "AR", "O", "AXr"}, {A, "AR", "#", "EHr"},
    {"^", "AS", "#", "EYs"}, {A, "A", "WA", "AX"}, {A, "AW", A, "AO"}, {" :", "ANY", A, "EHnIY"},
    {A, "A", "^+#", "EY"}, {"#:", "ALLY", A, "AXlIY"}, {N, "AL", "#", "AXl"}, {A, "AGAIN", A, "AXgEHn"},
    {"#:", "AG", "E", "IHj"}, {A, "A", "^+:#", "AE"}, {" :", "A", "^+ ", "EY"}, {A, "A", "^%", "EY"},
    {N, "ARR", A, "AXr"}, {A, "ARR", A, "AEr"}, {" :", "AR", N, "AAr"}, {A, "AR", N, "ER"},
    {A, "AR", A, "AAr"}, {A, "AIR", A, "EHr"}, {A, "AI", A, "EY"}, {A, "AY", A, "EY"}, {A, "AU", A, "AO"},
    {"#:", "AL", N, "AXl"}, {"#:", "ALS", N, "AXlz"}, {A, "ALK", A, "AOk"}, {A, "AL", "^", "AOl"},
    {" :", "ABLE", A, "EYbAXl"}, {A, "ABLE", A, "AXbAXl"}, {A, "ANG", "+", "EYnj"},
    {A, "A", A, "AE"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesB[] = {
    {N, "BE", "^#", "bIH"}, {A, "BEING", A, "bIYIHNG"}, {N, "BOTH", N, "bOWTH"}, {N, "BUS", "#", "bIHz"},
    {A, "BUIL", A, "bIHl"}, {A, "B", A, "b"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesC[] = {
    {N, "CH", "^", "k"}, {"^E", "CH", A, "k"}, {A, "CH", A, "CH"}, {" S", "CI", "#", "sAY"},
    {A, "CI", "A", "SH"}, {A, "CI", "O", "SH"}, {A, "CI", "EN", "SH"}, {A, "C", "+", "s"},
    {A, "CK", A, "k"}, {A, "COM", "%", "kAHm"}, {A, "C", A, "k"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesD[] = {
    {"#:", "DED", N, "dIHd"}, {".E", "D", N, "d"}, {"#:^E", "D", N, "t"}, {N, "DE", "^#", "dIH"},
    {N, "DO", N, "dUW"}, {N, "DOES", A, "dAHz"}, {N, "DOING", A, "dUWIHNG"}, {N, "DOW", A, "dAW"},
    {A, "DU", "A", "jUW"}, {A, "D", A, "d"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesE[] = {
    {"#:", "E", N, ""}, {"':^", "E", N, ""}, {" :", "E", N, "IY"}, {"#", "ED", N, "d"},
    {"#:", "E", "D ", ""}, {A, "EV", "ER", "EHv"}, {A, "E", "^%", "IY"}, {A, "ERI", "#", "IYrIY"},
    {A, "ERI", A, "EHrIH"}, {"#:", "ER", "#", "ER"}, {A, "ER", "#", "EHr"}, {A, "ER", A, "ER"},
    {N, "EVEN", A, "IYvEHn"}, {"#:", "E", "W", ""}, {"@", "EW", A, "UW"}, {A, "EW", A, "yUW"},
    {A, "E", "O", "IY"}, {"#:&", "ES", N, "IHz"}, {"#:", "E", "S ", ""}, {"#:", "ELY", N, "lIY"},
    {"#:", "EMENT", A, "mEHnt"}, {A, "EFUL", A, "fUHl"}, {A, "EE", A, "IY"}, {A, "EARN", A, "ERn"},
    {N, "EAR", "^", "ER"}, {A, "EAD", A, "EHd"}, {"#:", "EA", N, "IYAX"}, {A, "EA", "SU", "EH"},
    {A, "EA", A, "IY"}, {A, "EIGH", A, "EY"}, {A, "EI", A, "IY"}, {N, "EYE", A, "AY"}, {A, "EY", A, "IY"},
    {A, "EU", A, "yUW"}, {A, "E", A, "EH"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesF[] = {{A, "FUL", A, "fUHl"}, {A, "F", A, "f"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesG[] = {
    {A, "GIV", A, "gIHv"}, {N, "G", "I^", "g"}, {A, "GE", "T", "gEH"}, {"SU", "GGES", A, "gjEHs"},
    {A, "GG", A, "g"}, {" B#", "G", A, "g"}, {A, "G", "+", "j"}, {A, "GREAT", A, "grEYt"},
    {"#", "GH", A, ""}, {A, "G", A, "g"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesH[] = {
    {N, "HAV", A, "hAEv"}, {N, "HERE", A, "hIYr"}, {N, "HOUR", A, "AWER"}, {A, "HOW", A, "hAW"},
    {A, "H", "#", "h"}, {A, "H", A, ""}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesI[] = {
    {N, "IN", A, "IHn"}, {N, "I", N, "AY"}, {A, "IN", "D", "AYn"}, {A, "IER", A, "IYER"},
    {"#:R", "IED", A, "IYd"}, {A, "IED", N, "AYd"}, {A, "IEN", A, "IYEHn"}, {A, "IE", "T", "AYEH"},
    {" :", "I", "%", "AY"}, {A, "I", "%", "IY"}, {A, "IE", A, "IY"}, {A, "I", "^+:#", "IH"},
    {A, "IR", "#", "AYr"}, {A, "IZ", "%", "AYz"}, {A, "IS", "%", "AYz"}, {A, "I", "D%", "AY"},
    {"+^", "I", "^+", "IH"}, {A, "I", "T%", "AY"}, {"#:^", "I", "^+", "IH"}, {A, "I", "^+", "AY"},
    {A, "IR", A, "ER"}, {A, "IGH", A, "AY"}, {A, "ILD", A, "AYld"}, {A, "IGN", N, "AYn"},
    {A, "IGN", "^", "AYn"}, {A, "IGN", "%", "AYn"}, {A, "IQUE", A, "IYk"}, {A, "I", A, "IH"},
    {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesJ[] = {{A, "J", A, "j"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesK[] = {{N, "K", "N", ""}, {A, "K", A, "k"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesL[] = {
    {A, "LO", "C#", "lOW"}, {"L", "L", A, ""}, {"#:^", "L", "%", "AXl"}, {A, "LEAD", A, "lIYd"},
    {A, "L", A, "l"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesM[] = {{A, "MOV", A, "mUWv"}, {A, "M", A, "m"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesN[] = {
    {"E", "NG", "+", "nj"}, {A, "NG", "R", "NGg"}, {A, "NG", "#", "NGg"}, {A, "NGL", "%", "NGgAXl"},
    {A, "NG", A, "NG"}, {A, "NK", A, "NGk"}, {N, "NOW", N, "nAW"}, {A, "N", A, "n"},
    {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesO[] = {
    {A, "OF", N, "AXv"}, {A, "OROUGH", A, "EROW"}, {"#:", "OR", N, "ER"}, {"#:", "ORS", N, "ERz"},
    {A, "OR", A, "AOr"}, {N, "ONE", A, "wAHn"}, {A, "OW", A, "OW"}, {N, "OVER", A, "OWvER"},
    {A, "OV", A, "AHv"}, {A, "O", "^%", "OW"}, {A, "O", "^EN", "OW"}, {A, "O", "^I#", "OW"},
    {A, "OL", "D", "OWl"}, {A, "OUGHT", A, "AOt"}, {A, "OUGH", A, "AHf"}, {N, "OU", A, "AW"},
    {"H", "OU", "S#", "AW"}, {A, "OUS", A, "AXs"}, {A, "OUR", A, "AOr"}, {A, "OULD", A, "UHd"},
    {"^", "OU", "^L", "AH"}, {A, "OUP", A, "UWp"}, {A, "OU", A, "AW"}, {A, "OY", A, "OY"},
    {A, "OING", A, "OWIHNG"}, {A, "OI", A, "OY"}, {A, "OOR", A, "AOr"}, {A, "OOK", A, "UHk"},
    {A, "OOD", A, "UHd"}, {A, "OO", A, "UW"}, {A, "O", "E", "OW"}, {A, "O", N, "OW"}, {A, "OA", A, "OW"},
    {N, "ONLY", A, "OWnlIY"}, {N, "ONCE", A, "wAHns"}, {A, "ON'T", A, "OWnt"}, {"C", "O", "N", "AA"},
    {A, "O", "NG", "AO"}, {" :^", "O", "N", "AH"}, {"I", "ON", A, "AXn"}, {"#:", "ON", N, "AXn"},
    {"#^", "ON", A, "AXn"}, {A, "O", "ST ", "OW"}, {A, "OF", "^", "AOf"}, {A, "OTHER", A, "AHDHER"},
    {A, "OSS", N, "AOs"}, {"#:^", "OM", A, "AHm"}, {A, "O", A, "AA"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesP[] = {
    {A, "PH", A, "f"}, {A, "PEOP", A, "pIYp"}, {A, "POW", A, "pAW"}, {A, "PUT", N, "pUHt"},
    {A, "P", A, "p"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesQ[] = {
    {A, "QUAR", A, "kwAOr"}, {A, "QU", A, "kw"}, {A, "Q", A, "k"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesR[] = {{N, "RE", "^#", "rIY"}, {A, "R", A, "r"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesS[] = {
    {A, "SH", A, "SH"}, {"#", "SION", A, "ZHAXn"}, {A, "SOME", A, "sAHm"}, {"#", "SUR", "#", "ZHER"},
    {A, "SUR", "#", "SHER"}, {"#", "SU", "#", "ZHUW"}, {"#", "SSU", "#", "SHUW"}, {"#", "SED", N, "zd"},
    {"#", "S", "#", "z"}, {A, "SAID", A, "sEHd"}, {"^", "SION", A, "SHAXn"}, {A, "S", "S", ""},
    {".", "S", N, "z"}, {"#:.E", "S", N, "z"}, {"#:^##", "S", N, "z"}, {"#:^#", "S", N, "s"},
    {"U", "S", N, "s"}, {" :#", "S", N, "z"}, {N, "SCH", A, "sk"}, {A, "S", "C+", ""},
    {"#", "SM", A, "zm"}, {"#", "SN", "'", "zAXn"}, {A, "S", A, "s"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesT[] = {
    {N, "THE", N, "DHAX"}, {A, "TO", N, "tUW"}, {A, "THAT", N, "DHAEt"}, {N, "THIS", N, "DHIHs"},
    {N, "THEY", A, "DHEY"}, {N, "THERE", A, "DHEHr"}, {A, "THER", A, "DHER"}, {A, "THEIR", A, "DHEHr"},
    {N, "THAN", N, "DHAEn"}, {N, "THEM", N, "DHEHm"}, {A, "THESE", N, "DHIYz"}, {N, "THEN", A, "DHEHn"},
    {A, "THROUGH", A, "THrUW"}, {A, "THOSE", A, "DHOWz"}, {A, "THOUGH", N, "DHOW"}, {N, "THUS", A, "DHAHs"},
    {A, "TH", A, "TH"}, {"#:", "TED", N, "tIHd"}, {"S", "TI", "#N", "CH"}, {A, "TI", "O", "SH"},
    {A, "TI", "A", "SH"}, {A, "TIEN", A, "SHAXn"}, {A, "TUR", "#", "CHER"}, {A, "TU", "A", "CHUW"},
    {N, "TWO", A, "tUW"}, {A, "T", A, "t"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesU[] = {
    {N, "UN", "I", "yUWn"}, {N, "UN", A, "AHn"}, {N, "UPON", A, "AXpAOn"}, {"@", "UR", "#", "UHr"},
    {A, "UR", "#", "yUHr"}, {A, "UR", A, "ER"}, {A, "U", "^ ", "AH"}, {A, "U", "^^", "AH"},
    {A, "UY", A, "AY"}, {" G", "U", "#", ""}, {"G", "U", "%", ""}, {"G", "U", "#", "w"},
    {"#N", "U", A, "yUW"}, {"@", "U", A, "UW"}, {A, "U", A, "yUW"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesV[] = {{A, "VIEW", A, "vyUW"}, {A, "V", A, "v"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesW[] = {
    {N, "WERE", A, "wER"}, {A, "WA", "S", "wAA"}, {A, "WA", "T", "wAA"}, {A, "WHERE", A, "WHEHr"},
    {A, "WHAT", A, "WHAAt"}, {A, "WHOL", A, "hOWl"}, {A, "WHO", A, "hUW"}, {A, "WH", A, "WH"},
    {A, "WAR", A, "wAOr"}, {A, "WOR", "^", "wER"}, {A, "WR", A, "r"}, {A, "W", A, "w"},
    {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesX[] = {{A, "X", A, "ks"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesY[] = {
    {A, "YOUNG", A, "yAHNG"}, {N, "YOU", A, "yUW"}, {N, "YES", A, "yEHs"}, {N, "Y", A, "y"},
    {"#:^", "Y", N, "IY"}, {"#:^", "Y", "I", "IY"}, {" :", "Y", N, "AY"}, {" :", "Y", "#", "AY"},
    {" :", "Y", "^+:#", "IH"}, {" :", "Y", "^#", "AY"}, {A, "Y", A, "IH"}, {nullptr, nullptr, nullptr, nullptr}};
constexpr Rule kRulesZ[] = {{A, "Z", A, "z"}, {nullptr, nullptr, nullptr, nullptr}};
#undef N
#undef A

constexpr const Rule *kRules[26] = {kRulesA, kRulesB, kRulesC, kRulesD, kRulesE, kRulesF, kRulesG,
                                    kRulesH, kRulesI, kRulesJ, kRulesK, kRulesL, kRulesM, kRulesN,
                                    kRulesO, kRulesP, kRulesQ, kRulesR, kRulesS, kRulesT, kRulesU,
                                    kRulesV, kRulesW, kRulesX, kRulesY, kRulesZ};

// How each letter is said when a word is spelled out.
constexpr const char *kLetterNames[26] = {
    "EY", "bIY", "sIY", "dIY", "IY", "EHf", "jIY", "EYCH", "AY", "jEY", "kEY", "EHl", "EHm",
    "EHn", "OW", "pIY", "kyUW", "AAr", "EHs", "tIY", "yUW", "vIY", "dAHbAXlyUW", "EHks", "wAY", "zIY"};

inline bool isLetter(char c) { return (c >= 'A' && c <= 'Z') || c == '\''; }
inline bool isVowel(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; }
inline bool isConsonant(char c) { return c >= 'A' && c <= 'Z' && !isVowel(c); }
inline bool oneOf(char c, const char *set) { return c != 0 && std::strchr(set, c) != nullptr; }

// A word between spaces: `text[0]` and `text[length + 1]` are ' '.
class Word {
  public:
    char text[96];
    int length = 0;

    // Left context `context` (read right to left) ending just before `at`.
    bool leftMatches(const char *context, int at) const
    {
        int pos = at - 1;
        for(int i = static_cast<int>(std::strlen(context)) - 1; i >= 0; --i) {
            const char c = context[i];
            const char here = pos >= 0 ? text[pos] : ' ';
            if(isLetter(c)) {
                if(here != c) return false;
                --pos;
            } else if(c == ' ') {
                if(isLetter(here)) return false;
                --pos;
            } else if(c == '#') {
                if(!isVowel(here)) return false;
                while(pos >= 0 && isVowel(text[pos])) --pos;
            } else if(c == ':') {
                while(pos >= 0 && isConsonant(text[pos])) --pos;
            } else if(c == '^') {
                if(!isConsonant(here)) return false;
                --pos;
            } else if(c == '.') {
                if(!oneOf(here, "BDVGJLMNRWZ")) return false;
                --pos;
            } else if(c == '+') {
                if(!oneOf(here, "EIY")) return false;
                --pos;
            } else if(c == '@' || c == '&') {
                const char before = pos >= 1 ? text[pos - 1] : ' ';
                if(here == 'H' && oneOf(before, c == '@' ? "TCS" : "CS")) pos -= 2;
                else if(oneOf(here, c == '@' ? "TSRDLZNJ" : "SCGZXJ")) --pos;
                else return false;
            } else {
                return false;
            }
        }
        return true;
    }

    // Right context `context` starting at `at`.
    bool rightMatches(const char *context, int at) const
    {
        int pos = at;
        const int end = length + 2;
        auto get = [&](int p) { return p < end ? text[p] : ' '; };
        for(const char *p = context; *p; ++p) {
            const char c = *p;
            const char here = get(pos);
            if(isLetter(c)) {
                if(here != c) return false;
                ++pos;
            } else if(c == ' ') {
                if(isLetter(here)) return false;
                ++pos;
            } else if(c == '#') {
                if(!isVowel(here)) return false;
                while(isVowel(get(pos))) ++pos;
            } else if(c == ':') {
                while(isConsonant(get(pos))) ++pos;
            } else if(c == '^') {
                if(!isConsonant(here)) return false;
                ++pos;
            } else if(c == '.') {
                if(!oneOf(here, "BDVGJLMNRWZ")) return false;
                ++pos;
            } else if(c == '+') {
                if(!oneOf(here, "EIY")) return false;
                ++pos;
            } else if(c == '%') {
                // Suffix: E, ER, ES, ED, ELY or ING.
                if(here == 'E') {
                    ++pos;
                    const char next = get(pos);
                    if(next == 'L') {
                        if(get(pos + 1) != 'Y') return false;
                        pos += 2;
                    } else if(next == 'R' || next == 'S' || next == 'D') {
                        ++pos;
                    }
                } else if(here == 'I' && get(pos + 1) == 'N' && get(pos + 2) == 'G') {
                    pos += 3;
                } else {
                    return false;
                }
            } else if(c == '@' || c == '&') {
                const char next = get(pos + 1);
                if(oneOf(here, c == '@' ? "TCS" : "CS") && next == 'H') pos += 2;
                else if(oneOf(here, c == '@' ? "TSRDLZNJ" : "SCGZXJ")) ++pos;
                else return false;
            } else {
                return false;
            }
        }
        return true;
    }
};

} // namespace detail

// Text to an utterance. `text` is UTF-16 (a VST3 note-expression text).
class Translator {
  public:
    // Speaks `text` into `out`. With `phonetic`, the whole text is phoneme
    // codes, as between [brackets].
    void translate(const char16_t *text, int length, Utterance &out, bool phonetic = false)
    {
        out.clear();
        clauseStart_ = 0;
        stressPending_ = true;
        int i = 0;
        bool bracket = phonetic;
        while(i < length && !out.full) {
            const char16_t raw = text[i];
            if(bracket) {
                if(raw == ']') { bracket = phonetic; ++i; continue; }
                i += phoneticRun(text + i, length - i, out);
                continue;
            }
            if(raw == '[') { bracket = true; ++i; continue; }
            const char c = fold(raw);
            if(isWordChar(c)) {
                int size = 0;
                char word[80];
                while(i < length && size < static_cast<int>(sizeof(word)) - 1) {
                    const char w = fold(text[i]);
                    if(!isWordChar(w)) break;
                    word[size++] = w;
                    ++i;
                }
                while(i < length && isWordChar(fold(text[i]))) ++i; // Overlong words are cut.
                word[size] = 0;
                speakToken(word, size, out);
                continue;
            }
            if(c == '.' || c == '!' || c == '?' || c == ';') {
                endClause(out, c == '?' ? 2 : (c == '!' ? 1 : 0), c != ';');
            } else if(c == ',' || c == ':' || c == '-' || c == '(' || c == ')' || c == '"') {
                endClause(out, 0, false);
            }
            ++i;
        }
        endClause(out, 0, false);
        // Drop trailing pauses: the phrase ends with its last sound.
        while(out.count > 0 && out.code[out.count - 1] <= kWordGap) --out.count;
    }

  private:
    int clauseStart_ = 0;
    bool stressPending_ = true;

    static bool isWordChar(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\''; }

    // Upper-case ASCII letter, digit or punctuation for a UTF-16 unit; common
    // Latin-1 letters lose their accents; everything else becomes a space.
    static char fold(char16_t c)
    {
        if(c >= 'a' && c <= 'z') return static_cast<char>(c - 32);
        if(c >= 'A' && c <= 'Z') return static_cast<char>(c);
        if(c > 0 && c < 128 && std::strchr("0123456789.,!?;:-()\"'", static_cast<char>(c)) != nullptr)
            return static_cast<char>(c);
        if(c == 0x2019) return '\''; // Typographic apostrophe.
        if(c >= 0xC0 && c <= 0xFF) {
            static const char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOO OUUUUY  AAAAAAACEEEEIIIIDNOOOOO OUUUUY Y";
            const char folded = kLatin1[c - 0xC0];
            return folded == ' ' ? ' ' : folded;
        }
        return ' ';
    }

    static bool isFunctionWord(const char *word)
    {
        static const char *const kWords[] = {"A", "AN", "THE", "OF", "TO", "IN", "ON", "AT", "AND", "OR",
                                             "BUT", "IS", "IT", "AS", "BY", "FOR", "FROM", "WITH",
                                             "AM", "ARE", "WAS", "BE", "HAS", "HAD", "DO", "DOES", "THAT",
                                             "THIS", "IF", "SO", "NOT", "MY", "YOUR", "HIS", "HER", "ITS"};
        for(const char *candidate : kWords)
            if(std::strcmp(candidate, word) == 0) return true;
        return false;
    }

    // Phoneme codes up to ']' (or the end). Returns the units consumed.
    int phoneticRun(const char16_t *text, int length, Utterance &out)
    {
        char buffer[8] = {};
        int used = 0;
        while(used < length && text[used] != ']') {
            const char16_t c = text[used];
            if(c == ' ') { out.add(kWordGap); stressPending_ = true; ++used; continue; }
            if(c == ',' || c == '.' || c == '?' || c == '!') {
                endClause(out, c == '?' ? 2 : 0, c != ',');
                ++used;
                continue;
            }
            for(int k = 0; k < 2; ++k)
                buffer[k] = used + k < length && text[used + k] < 128 ? static_cast<char>(text[used + k]) : 0;
            buffer[2] = 0;
            int code = 0;
            const int size = detail::parsePhoneme(buffer, code);
            if(size == 0) { ++used; continue; }
            used += size;
            // A digit after a vowel: 1 stresses it, 0 reduces it.
            float pitch = 1.0f, duration = 1.0f;
            if(used < length && (text[used] == '1' || text[used] == '0')) {
                const bool stressed = text[used] == '1';
                pitch = stressed ? 1.1f : 0.97f;
                duration = stressed ? 1.2f : 0.8f;
                ++used;
            }
            out.add(code, pitch, duration);
        }
        return used > 0 ? used : 1;
    }

    // Read a word, a number or a spelled abbreviation.
    void speakToken(const char *token, int size, Utterance &out)
    {
        if(token[0] >= '0' && token[0] <= '9') {
            speakNumber(token, size, out);
            return;
        }
        bool vowel = false, letters = false;
        for(int i = 0; i < size; ++i) {
            if(token[i] >= 'A' && token[i] <= 'Z') letters = true;
            if(detail::isVowel(token[i]) || token[i] == 'Y') vowel = true;
            if(token[i] >= '0' && token[i] <= '9') {
                // "ST520": letters, then the number.
                char head[80];
                std::memcpy(head, token, i);
                head[i] = 0;
                speakToken(head, i, out);
                speakNumber(token + i, size - i, out);
                return;
            }
        }
        if(!letters) return;
        if(!vowel) {
            // No vowel: spell it ("ST", "TV").
            for(int i = 0; i < size; ++i) {
                if(token[i] < 'A' || token[i] > 'Z') continue;
                stressPending_ = true;
                emit(detail::kLetterNames[token[i] - 'A'], out, true, false);
                out.add(kWordGap);
            }
            return;
        }
        speakWord(token, size, out);
    }

    void speakWord(const char *token, int size, Utterance &out)
    {
        detail::Word word;
        if(size > static_cast<int>(sizeof(word.text)) - 3) size = static_cast<int>(sizeof(word.text)) - 3;
        word.text[0] = ' ';
        std::memcpy(word.text + 1, token, size);
        word.text[size + 1] = ' ';
        word.text[size + 2] = 0;
        word.length = size;
        stressPending_ = true;
        const bool function = isFunctionWord(token);
        int at = 1;
        while(at <= size) {
            const char letter = word.text[at];
            if(letter < 'A' || letter > 'Z') { ++at; continue; }
            const detail::Rule *rule = detail::kRules[letter - 'A'];
            bool matched = false;
            for(; rule->match; ++rule) {
                const int length = static_cast<int>(std::strlen(rule->match));
                if(std::strncmp(word.text + at, rule->match, length) != 0) continue;
                if(!word.leftMatches(rule->left, at) || !word.rightMatches(rule->right, at + length)) continue;
                emit(rule->out, out, !function, function);
                at += length;
                matched = true;
                break;
            }
            if(!matched) ++at;
        }
        out.add(kWordGap);
    }

    // Append a phoneme spelling; the word's first vowel takes the stress.
    void emit(const char *spelling, Utterance &out, bool stress, bool reduced)
    {
        while(*spelling) {
            int code = 0;
            const int size = detail::parsePhoneme(spelling, code);
            if(size == 0) { ++spelling; continue; }
            spelling += size;
            float pitch = 1.0f, duration = 1.0f;
            if(isVowelPhoneme(code)) {
                if(stress && stressPending_) { pitch = 1.1f; duration = 1.15f; }
                else if(reduced || code == kAX) { pitch = 0.98f; duration = 0.8f; }
                stressPending_ = false;
            }
            out.add(code, pitch, duration);
        }
    }

    // Close a clause: declining pitch through it, a lengthened last vowel,
    // a rise for questions (kind 2) or a lift for exclamations (kind 1),
    // and a pause (long at sentence ends).
    void endClause(Utterance &out, int kind, bool sentence)
    {
        const int start = clauseStart_, end = out.count;
        int sounds = 0, lastVowel = -1;
        for(int i = start; i < end; ++i) {
            if(out.code[i] > kWordGap) ++sounds;
            if(isVowelPhoneme(out.code[i])) lastVowel = i;
        }
        if(sounds > 0) {
            int index = 0;
            for(int i = start; i < end; ++i) {
                if(out.code[i] <= kWordGap) continue;
                const float progress = sounds > 1 ? static_cast<float>(index) / (sounds - 1) : 0.0f;
                float contour = 1.08f - 0.18f * progress;
                if(kind == 1) contour += 0.08f * (1.0f - progress);
                if(kind == 2 && lastVowel >= 0 && i >= lastVowel - 1) contour = 1.25f + 0.1f * progress;
                out.pitch[i] *= contour;
                ++index;
            }
            if(lastVowel >= 0) out.duration[lastVowel] *= 1.3f;
            out.add(sentence ? kPauseLong : kPauseShort);
        }
        clauseStart_ = out.count;
        stressPending_ = true;
    }

    // Numbers as words: digits up to 999,999,999,999, then digit by digit;
    // a decimal point reads "point".
    void speakNumber(const char *digits, int size, Utterance &out)
    {
        int whole = 0;
        while(whole < size && digits[whole] >= '0' && digits[whole] <= '9') ++whole;
        if(whole > 12 || (whole > 1 && digits[0] == '0')) {
            for(int i = 0; i < whole; ++i) sayDigit(digits[i] - '0', out);
            return;
        }
        long long value = 0;
        for(int i = 0; i < whole; ++i) value = value * 10 + (digits[i] - '0');
        if(value == 0) { saySmall("ZERO", out); return; }
        if(whole == 4 && value >= 1100 && (value < 2000 || value >= 2010)) {
            // Years: "nineteen eighty seven", "nineteen hundred", "twenty oh five".
            const int high = static_cast<int>(value / 100), low = static_cast<int>(value % 100);
            sayHundreds(high, out);
            if(low == 0) saySmall("HUNDRED", out);
            else {
                if(low < 10) saySmall("OH", out);
                sayHundreds(low, out);
            }
            return;
        }
        static const long long kScales[] = {1000000000LL, 1000000LL, 1000LL};
        static const char *const kNames[] = {"BILLION", "MILLION", "THOUSAND"};
        for(int s = 0; s < 3; ++s) {
            if(value >= kScales[s]) {
                sayHundreds(static_cast<int>(value / kScales[s]), out);
                saySmall(kNames[s], out);
                value %= kScales[s];
            }
        }
        if(value > 0) sayHundreds(static_cast<int>(value), out);
    }

    void sayDigit(int digit, Utterance &out)
    {
        static const char *const kDigits[] = {"ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE"};
        saySmall(kDigits[digit], out);
    }

    void sayHundreds(int value, Utterance &out)
    {
        static const char *const kOnes[] = {"", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE",
                                            "TEN", "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN",
                                            "SIXTEEN", "SEVENTEEN", "EIGHTEEN", "NINETEEN"};
        static const char *const kTens[] = {"", "", "TWENTY", "THIRTY", "FORTY", "FIFTY", "SIXTY", "SEVENTY", "EIGHTY", "NINETY"};
        if(value >= 100) {
            saySmall(kOnes[value / 100], out);
            saySmall("HUNDRED", out);
            value %= 100;
        }
        if(value >= 20) {
            saySmall(kTens[value / 10], out);
            value %= 10;
        }
        if(value > 0) saySmall(kOnes[value], out);
    }

    void saySmall(const char *word, Utterance &out) { speakWord(word, static_cast<int>(std::strlen(word)), out); }
};

} // namespace mla_speech
