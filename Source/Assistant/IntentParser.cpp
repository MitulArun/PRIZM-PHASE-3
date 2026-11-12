/*
  ==============================================================================

    PRIZM V2 - natural-language intent parser.

  ==============================================================================
*/

#include "IntentParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <initializer_list>

namespace prizm::assistant
{

namespace
{
    //==========================================================================
    // Text helpers

    bool isWordChar (char c) noexcept
    {
        return std::isalnum ((unsigned char) c) != 0 || c == '\'';
    }

    /** Boundary-aware search: "hi" does not match "this" or "high". */
    size_t findWord (const std::string& text, const std::string& phrase, size_t from = 0)
    {
        if (phrase.empty())
            return std::string::npos;

        for (size_t pos = text.find (phrase, from); pos != std::string::npos; pos = text.find (phrase, pos + 1))
        {
            const bool startOk = pos == 0 || ! isWordChar (text[pos - 1]) || ! isWordChar (phrase.front());
            const size_t end = pos + phrase.size();
            const bool endOk = end >= text.size() || ! isWordChar (text[end]) || ! isWordChar (phrase.back());
            if (startOk && endOk)
                return pos;
        }

        return std::string::npos;
    }

    bool hasWord (const std::string& text, const std::string& phrase)
    {
        return findWord (text, phrase) != std::string::npos;
    }

    bool hasAny (const std::string& text, std::initializer_list<const char*> phrases)
    {
        for (auto* p : phrases)
            if (hasWord (text, p))
                return true;
        return false;
    }

    int countWords (const std::string& text)
    {
        int n = 0;
        bool inWord = false;
        for (char c : text)
        {
            const bool w = isWordChar (c);
            if (w && ! inWord)
                ++n;
            inWord = w;
        }
        return n;
    }

    //==========================================================================
    // Numbers with units

    struct NumberToken
    {
        float value;
        std::string unit;   // "hz", "k", "db", "lufs", "ms", "%", ":1", ""
        size_t pos;
    };

    std::vector<NumberToken> extractNumbers (const std::string& s)
    {
        std::vector<NumberToken> out;
        size_t i = 0;

        while (i < s.size())
        {
            const char c = s[i];
            const bool signStart = (c == '-' || c == '+') && i + 1 < s.size()
                                    && (std::isdigit ((unsigned char) s[i + 1]) || s[i + 1] == '.')
                                    && (i == 0 || ! isWordChar (s[i - 1]));
            const bool digitStart = std::isdigit ((unsigned char) c) && (i == 0 || ! std::isalpha ((unsigned char) s[i - 1]));

            if (! signStart && ! digitStart)
            {
                ++i;
                continue;
            }

            char* end = nullptr;
            const float v = std::strtof (s.c_str() + i, &end);
            size_t j = (size_t) (end - s.c_str());
            const size_t numberPos = i;

            if (j == i)
            {
                ++i;
                continue;
            }

            // Ratio "3:1" or "3 to 1"
            std::string unit;
            size_t k = j;
            if (k + 1 < s.size() && s[k] == ':' && std::isdigit ((unsigned char) s[k + 1]))
            {
                unit = ":1";
                k += 2;
            }
            else
            {
                while (k < s.size() && s[k] == ' ')
                    ++k;

                size_t u = k;
                while (u < s.size() && (std::isalpha ((unsigned char) s[u]) || s[u] == '%'))
                    ++u;

                const std::string word = s.substr (k, u - k);
                if (word == "hz" || word == "hertz")                    { unit = "hz"; k = u; }
                else if (word == "khz" || word == "k" || word == "kilohertz") { unit = "k"; k = u; }
                else if (word == "db" || word == "dbfs" || word == "decibels" || word == "decibel") { unit = "db"; k = u; }
                else if (word == "dbtp" || word == "tp")               { unit = "dbtp"; k = u; }
                else if (word == "lufs" || word == "lu")               { unit = "lufs"; k = u; }
                else if (word == "ms" || word == "milliseconds")       { unit = "ms"; k = u; }
                else if (word == "%" || word == "percent")             { unit = "%"; k = u; }
                else if (word == "to" && s.compare (u, 2, " 1") == 0)  { unit = ":1"; k = u + 2; }
                else                                                   { k = j; }
            }

            out.push_back ({ v, unit, numberPos });
            i = std::max (k, j);
        }

        return out;
    }

    std::optional<float> firstWithUnit (const std::vector<NumberToken>& nums, std::initializer_list<const char*> units)
    {
        for (auto& n : nums)
            for (auto* u : units)
                if (n.unit == u)
                    return n.value;
        return std::nullopt;
    }

    std::optional<float> frequencyIn (const std::vector<NumberToken>& nums)
    {
        for (auto& n : nums)
        {
            if (n.unit == "hz") return n.value;
            if (n.unit == "k")  return n.value * 1000.0f;
        }
        return std::nullopt;
    }

    std::optional<float> plainNumberIn (const std::vector<NumberToken>& nums)
    {
        for (auto& n : nums)
            if (n.unit.empty())
                return n.value;
        return std::nullopt;
    }

    //==========================================================================
    // Modifiers

    float intensityOf (const std::string& c)
    {
        if (hasAny (c, { "extremely", "massively", "drastically", "maximum", "way more", "a ton", "insanely" }))
            return 2.0f;
        if (hasAny (c, { "much", "a lot", "lots", "really", "very", "significantly", "heavily", "heavy", "strongly",
                         "seriously", "super", "way too", "considerably", "noticeably", "big" }))
            return 1.5f;
        if (hasAny (c, { "a little", "a bit", "a tiny bit", "slightly", "subtle", "subtly", "a touch", "touch of", "tiny",
                         "gently", "gentle", "lightly", "a hint", "hint of", "barely", "small", "mild", "mildly", "bit" }))
            return 0.5f;
        return 1.0f;
    }

    bool isNegated (const std::string& c)
    {
        if (hasAny (c, { "not enough", "isn't enough", "lacks", "lacking", "missing", "needs more", "need more" }))
            return false;

        return hasAny (c, { "less", "reduce", "reduced", "decrease", "lower", "lowered", "cut", "remove", "tame", "turn down",
                            "too", "fewer", "drop", "kill", "not as", "minus", "get rid of", "take out", "take away",
                            "pull back", "back off", "dial back", "dial down", "soften", "calm" });
    }

    bool isDesireContext (const std::string& c)
    {
        return hasAny (c, { "make it", "make the", "make my", "make this", "i want", "want it", "should be", "could be",
                            "needs to be", "need it", "go", "turn it", "more", "add", "give it", "give me", "bring" });
    }

    //==========================================================================
    // Descriptor lexicon

    struct Descriptor
    {
        const char* phrase;
        IntentType type;
        float desireDir;       // direction when the user asks for this quality
        float complaintDir;    // direction when the word describes a problem (0 = not a complaint word)
        bool comparative;      // "-er" forms are always requests
    };

    const Descriptor descriptors[] =
    {
        // Warmth / body
        { "warmer",      IntentType::Warmth,     1, 0, true },
        { "warmth",      IntentType::Warmth,     1, 0, false },
        { "warm",        IntentType::Warmth,     1, 0, false },
        { "fuller",      IntentType::Warmth,     1, 0, true },
        { "full",        IntentType::Warmth,     1, 0, false },
        { "body",        IntentType::Warmth,     1, 0, false },
        { "thicker",     IntentType::Warmth,     1, 0, true },
        { "fatter",      IntentType::Warmth,     1, 0, true },
        { "richer",      IntentType::Warmth,     1, 0, true },
        { "thinner",     IntentType::Warmth,    -1, 0, true },
        { "thin",        IntentType::Warmth,    -1, 1, false },
        { "cold",        IntentType::Warmth,    -1, 1, false },

        // Brightness
        { "brighter",    IntentType::Brightness, 1, 0, true },
        { "brightness",  IntentType::Brightness, 1, 0, false },
        { "bright",      IntentType::Brightness, 1, -1, false },
        { "treble",      IntentType::Brightness, 1, 0, false },
        { "highs",       IntentType::Brightness, 1, 0, false },
        { "top end",     IntentType::Brightness, 1, 0, false },
        { "crisper",     IntentType::Brightness, 1, 0, true },
        { "crisp",       IntentType::Brightness, 1, 0, false },
        { "sparkle",     IntentType::Brightness, 1, 0, false },
        { "shine",       IntentType::Brightness, 1, 0, false },
        { "clearer",     IntentType::Brightness, 1, 0, true },
        { "clarity",     IntentType::Brightness, 1, 0, false },
        { "darker",      IntentType::Brightness, -1, 0, true },
        { "duller",      IntentType::Brightness, -1, 0, true },
        { "dark",        IntentType::Brightness, -1, 1, false },
        { "dull",        IntentType::Brightness, -1, 1, false },
        { "muffled",     IntentType::Brightness, -1, 1, false },

        // Air
        { "airier",      IntentType::Air,        1, 0, true },
        { "airy",        IntentType::Air,        1, 0, false },
        { "air",         IntentType::Air,        1, 0, false },
        { "open",        IntentType::Air,        1, 0, false },

        // Low end
        { "sub bass",    IntentType::Sub,        1, 0, false },
        { "subs",        IntentType::Sub,        1, 0, false },
        { "sub",         IntentType::Sub,        1, 0, false },
        { "808",         IntentType::Sub,        1, 0, false },
        { "low end",     IntentType::Bass,       1, 0, false },
        { "low-end",     IntentType::Bass,       1, 0, false },
        { "bass",        IntentType::Bass,       1, 0, false },
        { "lows",        IntentType::Bass,       1, 0, false },
        { "bottom",      IntentType::Bass,       1, 0, false },
        { "kick",        IntentType::Bass,       1, 0, false },
        { "boomy",       IntentType::Bass,      -1, -1, false },
        { "boom",        IntentType::Bass,      -1, -1, false },
        { "bassy",       IntentType::Bass,       1, -1, false },

        // Problems
        { "muddy",       IntentType::Mud,        -1, -1, false },
        { "mud",         IntentType::Mud,        -1, -1, false },
        { "murky",       IntentType::Mud,        -1, -1, false },
        { "cloudy",      IntentType::Mud,        -1, -1, false },
        { "boxy",        IntentType::Boxy,       -1, -1, false },
        { "nasal",       IntentType::Boxy,       -1, -1, false },
        { "honky",       IntentType::Boxy,       -1, -1, false },
        { "harshness",   IntentType::Harsh,      -1, -1, false },
        { "harsh",       IntentType::Harsh,      -1, -1, false },
        { "shrill",      IntentType::Harsh,      -1, -1, false },
        { "piercing",    IntentType::Harsh,      -1, -1, false },
        { "fatiguing",   IntentType::Harsh,      -1, -1, false },
        { "brittle",     IntentType::Harsh,      -1, -1, false },
        { "sibilance",   IntentType::Sibilance,  -1, -1, false },
        { "sibilant",    IntentType::Sibilance,  -1, -1, false },
        { "esses",       IntentType::Sibilance,  -1, -1, false },

        // Presence
        { "presence",    IntentType::Presence,   1, 0, false },
        { "vocals",      IntentType::Presence,   1, 0, false },
        { "vocal",       IntentType::Presence,   1, 0, false },
        { "upfront",     IntentType::Presence,   1, 0, false },
        { "forward",     IntentType::Presence,   1, 0, false },

        // Dynamics
        { "punchier",    IntentType::Punch,      1, 0, true },
        { "punchy",      IntentType::Punch,      1, 0, false },
        { "punch",       IntentType::Punch,      1, 0, false },
        { "impact",      IntentType::Punch,      1, 0, false },
        { "hit harder",  IntentType::Punch,      1, 0, true },
        { "glue",        IntentType::Glue,       1, 0, false },
        { "glued",       IntentType::Glue,       1, 0, false },
        { "cohesive",    IntentType::Glue,       1, 0, false },
        { "smoother",    IntentType::Glue,       1, 0, true },
        { "smooth",      IntentType::Glue,       1, 0, false },
        { "polished",    IntentType::Glue,       1, 0, false },
        { "tighter",     IntentType::Tight,      1, 0, true },
        { "tight",       IntentType::Tight,      1, 0, false },
        { "controlled",  IntentType::Tight,      1, 0, false },
        { "more dynamic", IntentType::MoreDynamic, 1, 0, true },
        { "dynamics",    IntentType::MoreDynamic, 1, 0, false },
        { "breathe",     IntentType::MoreDynamic, 1, 0, false },
        { "squashed",    IntentType::MoreDynamic, 1, 1, false },
        { "crushed",     IntentType::MoreDynamic, 1, 1, false },
        { "overcompressed", IntentType::MoreDynamic, 1, 1, false },
        { "over-compressed", IntentType::MoreDynamic, 1, 1, false },
        { "lifeless",    IntentType::MoreDynamic, 1, 1, false },

        // Stereo
        { "wider",       IntentType::Wider,      1, 0, true },
        { "width",       IntentType::Wider,      1, 0, false },
        { "wide",        IntentType::Wider,      1, -1, false },
        { "spacious",    IntentType::Wider,      1, 0, false },
        { "bigger",      IntentType::Wider,      1, 0, true },
        { "narrower",    IntentType::Wider,     -1, 0, true },
        { "narrow",      IntentType::Wider,     -1, 1, false },
        { "focused",     IntentType::Wider,     -1, 0, false },
    };

    const Descriptor* matchDescriptor (const std::string& clause, size_t& posOut, std::vector<bool>& used)
    {
        for (size_t d = 0; d < std::size (descriptors); ++d)
        {
            if (used[d])
                continue;

            const size_t pos = findWord (clause, descriptors[d].phrase);
            if (pos != std::string::npos)
            {
                used[d] = true;
                posOut = pos;
                return &descriptors[d];
            }
        }

        return nullptr;
    }

    std::vector<std::string> splitClauses (const std::string& text)
    {
        std::string s = " " + text + " ";

        for (const char* sep : { " but ", " and then ", " then ", " and ", " also ", " plus ", " while ", " though " })
        {
            for (size_t pos = s.find (sep); pos != std::string::npos; pos = s.find (sep, pos + 1))
                s.replace (pos, std::string (sep).size(), " | ");
        }

        std::vector<std::string> out;
        std::string current;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const char c = s[i];
            const bool sentenceStop = (c == '.' || c == '!' || c == '?') && (i + 1 >= s.size() || s[i + 1] == ' ');
            if (c == '|' || c == ',' || c == ';' || sentenceStop)
            {
                out.push_back (current);
                current.clear();
            }
            else
            {
                current += c;
            }
        }
        out.push_back (current);

        std::vector<std::string> trimmed;
        for (auto& c : out)
        {
            const auto b = c.find_first_not_of (' ');
            const auto e = c.find_last_not_of (' ');
            if (b != std::string::npos)
                trimmed.push_back (c.substr (b, e - b + 1));
        }
        return trimmed;
    }

    Module moduleIn (const std::string& c)
    {
        if (hasAny (c, { "limiter", "maximizer", "maximiser", "limiting" }))             return Module::Limiter;
        if (hasAny (c, { "compressor", "compression", "comp", "dynamics module" }))       return Module::Compressor;
        if (hasAny (c, { "saturation", "saturator", "exciter", "distortion" }))           return Module::Saturation;
        if (hasAny (c, { "imager", "stereo imager", "width module", "stereo widener" }))  return Module::Imager;
        if (hasAny (c, { "eq", "equalizer", "equaliser", "equalization" }))               return Module::EQ;
        return Module::Global;
    }

    struct Platform { const char* word; const char* name; float lufs; float ceiling; };

    const Platform platforms[] =
    {
        { "spotify",      "Spotify",         -14.0f, -1.0f },
        { "apple music",  "Apple Music",     -16.0f, -1.0f },
        { "itunes",       "Apple Music",     -16.0f, -1.0f },
        { "youtube",      "YouTube",         -14.0f, -1.0f },
        { "tidal",        "Tidal",           -14.0f, -1.0f },
        { "amazon",       "Amazon Music",    -14.0f, -2.0f },
        { "deezer",       "Deezer",          -15.0f, -1.0f },
        { "soundcloud",   "SoundCloud",      -10.0f, -1.0f },
        { "streaming",    "streaming",       -14.0f, -1.0f },
        { "podcast",      "podcast",         -16.0f, -1.0f },
        { "broadcast",    "broadcast (EBU R128)", -23.0f, -1.0f },
        { "r128",         "broadcast (EBU R128)", -23.0f, -1.0f },
        { "club",         "club / DJ",        -8.0f, -0.5f },
        { "dj",           "club / DJ",        -8.0f, -0.5f },
        { "edm",          "club / DJ",        -8.0f, -0.5f },
        { "competitive",  "competitive loudness", -9.0f, -0.5f },
        { "cd",           "CD",              -10.0f, -0.3f },
    };
}

//==============================================================================
std::string normaliseText (const std::string& text)
{
    std::string s;
    s.reserve (text.size());

    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = (unsigned char) text[i];

        // UTF-8 minus sign / en dash / em dash -> '-'
        if (c == 0xE2 && i + 2 < text.size() && (unsigned char) text[i + 1] == 0x88 && (unsigned char) text[i + 2] == 0x92)
        { s += '-'; i += 2; continue; }
        if (c == 0xE2 && i + 2 < text.size() && (unsigned char) text[i + 1] == 0x80
            && ((unsigned char) text[i + 2] == 0x93 || (unsigned char) text[i + 2] == 0x94))
        { s += '-'; i += 2; continue; }
        // Curly apostrophes -> '
        if (c == 0xE2 && i + 2 < text.size() && (unsigned char) text[i + 1] == 0x80 && (unsigned char) text[i + 2] == 0x99)
        { s += '\''; i += 2; continue; }

        if (c < 0x80)
            s += (char) std::tolower (c);
        else
            s += ' ';
    }

    // Collapse whitespace.
    std::string out;
    bool space = false;
    for (char c : s)
    {
        const bool isSpace = std::isspace ((unsigned char) c) != 0;
        if (isSpace)
        {
            if (! space && ! out.empty())
                out += ' ';
        }
        else
        {
            out += c;
        }
        space = isSpace;
    }

    while (! out.empty() && out.back() == ' ')
        out.pop_back();

    return out;
}

ParsedMessage parseMessage (const std::string& text)
{
    ParsedMessage result;
    const std::string all = normaliseText (text);
    if (all.empty())
        return result;

    result.isQuestion = all.find ('?') != std::string::npos
                        || hasAny (all, { "what", "how", "why", "which", "is it", "are there", "does it", "can you tell" });

    auto add = [&result] (Intent i) { result.intents.push_back (std::move (i)); };
    auto simple = [] (IntentType t, const std::string& phrase) { Intent i { t }; i.phrase = phrase; return i; };

    //--------------------------------------------------------------------------
    // Whole-message commands
    const int words = countWords (all);

    // Approve / reject a pending proposal - only when the message isn't also a new request.
    const bool applyCandidate = words <= 6
        && hasAny (all, { "yes", "yep", "yeah", "apply", "apply it", "do it", "go ahead", "sounds good", "ok", "okay",
                          "sure", "accept", "approve", "approved", "confirm", "let's do it", "please do", "keep it" })
        && ! hasAny (all, { "don't", "do not", "no" });

    const bool dismissCandidate = words <= 5
        && hasAny (all, { "no", "nope", "cancel", "dismiss", "reject", "don't", "do not", "nah", "skip", "never mind", "nevermind" });

    if (hasAny (all, { "undo", "go back", "revert", "take that back", "step back" }))
        add (simple (IntentType::Undo, "undo"));
    if (hasWord (all, "redo"))
        add (simple (IntentType::Redo, "redo"));

    if (hasAny (all, { "reset", "start over", "start again", "clear all", "clear everything", "everything off", "all off",
                       "turn everything off", "remove everything", "bypass everything", "back to flat", "defaults", "default settings" })
        || all == "off" || all == "clear")
        add (simple (IntentType::Reset, "reset"));

    if (hasAny (all, { "help", "commands", "what can you do", "what do you do", "how do i use", "how does this work" }))
        add (simple (IntentType::Help, "help"));

    if (words <= 6 && hasAny (all, { "hi", "hello", "hey", "yo", "good morning", "good evening", "greetings" }))
        add (simple (IntentType::Greeting, "hello"));

    if (hasAny (all, { "thanks", "thank you", "thx", "cheers", "appreciate it" }))
        add (simple (IntentType::Thanks, "thanks"));

    if (hasAny (all, { "explain", "what did you change", "what did you do", "what changed", "why did you", "what was changed" }))
        add (simple (IntentType::Explain, "explain"));

    const bool mentionsReference = hasAny (all, { "reference", "ref track", "reference track" });

    if (mentionsReference && hasAny (all, { "load", "import", "open", "choose", "select", "pick", "add a", "add my", "browse", "upload" }))
        add (simple (IntentType::LoadReference, "load reference"));

    if (hasAny (all, { "match the reference", "match my reference", "match reference", "like the reference", "like my reference",
                       "closer to the reference", "closer to my reference", "reference match", "match it", "sound like" })
        || (mentionsReference && hasWord (all, "match")))
        add (simple (IntentType::MatchReference, "match reference"));

    const bool analyse = hasAny (all, { "analyze", "analyse", "analysis", "diagnose", "what's wrong", "what is wrong", "how does it sound",
                                        "how does my mix sound", "how's my mix", "how is my mix", "check my mix", "check the mix",
                                        "feedback", "how close", "inspect", "evaluate", "assess", "critique", "review my" })
                         || (mentionsReference && hasAny (all, { "compare", "comparison", "difference", "versus", "vs" }));
    if (analyse)
        add (simple (IntentType::Analyze, "analyze"));
    else if (hasAny (all, { "compare", "a/b", "ab", "before and after", "before/after", "original", "bypass", "hear the difference",
                            "unprocessed", "dry signal" }) && moduleIn (all) == Module::Global)
        add (simple (IntentType::Compare, "compare"));

    if (hasAny (all, { "mono check", "check mono", "listen in mono", "in mono", "mono compatibility", "sum to mono", "mono button" })
        && ! hasAny (all, { "mono bass", "bass mono", "mono the bass", "mono low end", "low end mono", "mono below" }))
        add (simple (IntentType::MonoCheck, "mono check"));

    if (hasAny (all, { "master", "mastering", "auto master", "automaster", "auto-master", "professional", "release ready",
                       "radio ready", "streaming ready", "finish my track", "finish the track", "finish it", "suggest a chain",
                       "build a chain", "recommend a chain", "set up a chain", "make a chain", "signal chain", "fix my mix",
                       "improve my mix", "improve the mix", "make it sound better", "make it sound good", "what should i do",
                       "what would you suggest", "what do you suggest", "make it sound pro" })
        && ! hasAny (all, { "master bus", "master channel", "master fader", "master track" })
        && ! result.has (IntentType::MatchReference))
        add (simple (IntentType::Master, "master"));

    if (result.isQuestion && hasAny (all, { "loud", "lufs", "level", "levels", "peak", "peaks", "meter", "meters", "loudness", "clipping", "status" })
        && ! hasAny (all, { "louder", "make it", "set", "target", "for spotify", "master" }))
        add (simple (IntentType::Status, "status"));

    //--------------------------------------------------------------------------
    // Per-clause tweaks
    for (const auto& clause : splitClauses (all))
    {
        // "open / load my reference" is a workflow command, not a tonal request.
        if (hasAny (clause, { "reference", "ref track" })
            && hasAny (clause, { "load", "import", "open", "choose", "select", "pick", "browse", "upload", "add" }))
            continue;

        const auto nums = extractNumbers (clause);
        const float intensity = intensityOf (clause);
        const bool negated = isNegated (clause);
        const bool desire = isDesireContext (clause);
        const Module mod = moduleIn (clause);

        // Platform / LUFS target / ceiling
        {
            const Platform* platform = nullptr;
            for (auto& p : platforms)
                if (hasWord (clause, p.word))
                {
                    platform = &p;
                    break;
                }

            auto lufs = firstWithUnit (nums, { "lufs" });
            if (! lufs && hasAny (clause, { "lufs", "loudness target", "target" }))
            {
                auto plain = plainNumberIn (nums);
                if (plain && *plain < -4.0f && *plain > -40.0f)
                    lufs = plain;
            }

            if (lufs || platform)
            {
                Intent i { IntentType::LoudnessTarget };
                i.lufs = lufs ? *lufs : platform->lufs;
                if (i.lufs && *i.lufs > 0.0f)
                    i.lufs = -*i.lufs;   // "14 LUFS" almost always means -14
                i.ceilingDb = platform != nullptr ? platform->ceiling : -1.0f;
                i.platform = platform != nullptr ? platform->name : "";
                i.phrase = clause;
                add (i);
            }

            if (hasAny (clause, { "ceiling", "true peak", "dbtp" }))
            {
                auto c = firstWithUnit (nums, { "dbtp", "db" });
                if (! c)
                    c = plainNumberIn (nums);
                if (c)
                {
                    Intent i { IntentType::Ceiling };
                    i.gainDb = *c > 0.0f ? -*c : *c;
                    i.phrase = clause;
                    add (i);
                }
            }
        }

        // Module on/off
        if (mod != Module::Global)
        {
            const bool off = hasAny (clause, { "turn off", "switch off", "disable", "bypass", "remove the", "kill", "get rid of",
                                               "no more", "without", "deactivate" })
                             || (hasWord (clause, "off") && ! hasWord (clause, "on"));
            const bool on = hasAny (clause, { "turn on", "switch on", "enable", "activate", "add a", "add the", "add some",
                                              "use the", "use a", "engage", "bring back" })
                            || (hasWord (clause, "on") && ! off);

            const bool settingWords = hasAny (clause, { "threshold", "ratio", "attack", "release", "makeup", "knee", "drive",
                                                        "ceiling", "gain", "mix", "width" });

            if (off && ! settingWords)
            {
                Intent i { IntentType::ModuleOff };
                i.module = mod;
                i.phrase = clause;
                add (i);
                continue;
            }

            if (on && ! settingWords && mod != Module::Saturation && mod != Module::Compressor)
            {
                Intent i { IntentType::ModuleOn };
                i.module = mod;
                i.phrase = clause;
                add (i);
                continue;
            }
        }

        // Compressor / limiter settings ("threshold -20", "ratio 3:1", "attack 10 ms")
        {
            struct FieldWord { const char* word; CompField field; };
            const FieldWord fields[] = { { "threshold", CompField::Threshold }, { "ratio", CompField::Ratio },
                                         { "attack", CompField::Attack }, { "release", CompField::Release },
                                         { "makeup", CompField::Makeup }, { "make-up", CompField::Makeup },
                                         { "knee", CompField::Knee } };

            for (auto& f : fields)
            {
                const size_t pos = findWord (clause, f.word);
                if (pos == std::string::npos)
                    continue;

                std::optional<float> value;
                for (auto& n : nums)
                    if (n.pos > pos) { value = n.value; break; }
                if (! value)
                    for (auto& n : nums) { value = n.value; break; }

                auto ratio = firstWithUnit (nums, { ":1" });
                if (f.field == CompField::Ratio && ratio)
                    value = ratio;

                if (! value)
                    continue;

                Intent i { IntentType::CompSetting };
                i.compField = f.field;
                i.plainNumber = value;
                i.module = (mod == Module::Limiter && f.field == CompField::Release) ? Module::Limiter : Module::Compressor;
                i.phrase = clause;
                add (i);
            }

            if (! result.has (IntentType::CompSetting) && firstWithUnit (nums, { ":1" }) && mod == Module::Compressor)
            {
                Intent i { IntentType::CompSetting };
                i.compField = CompField::Ratio;
                i.plainNumber = firstWithUnit (nums, { ":1" });
                i.module = Module::Compressor;
                add (i);
            }
        }

        // Explicit filters
        const auto freq = frequencyIn (nums);

        if (hasAny (clause, { "high pass", "highpass", "high-pass", "low cut", "lowcut", "low-cut", "hpf", "rumble", "subsonic" }))
        {
            Intent i { IntentType::LowCut };
            i.freqHz = freq;
            i.direction = hasAny (clause, { "remove the low cut", "turn off", "disable", "no " }) ? -1.0f : 1.0f;
            i.phrase = clause;
            add (i);
            continue;
        }

        if (hasAny (clause, { "low pass", "lowpass", "low-pass", "high cut", "highcut", "high-cut", "lpf" }))
        {
            Intent i { IntentType::HighCut };
            i.freqHz = freq;
            i.direction = hasAny (clause, { "turn off", "disable", "remove" }) ? -1.0f : 1.0f;
            i.phrase = clause;
            add (i);
            continue;
        }

        if (hasAny (clause, { "mono bass", "bass mono", "mono the bass", "mono low end", "low end mono", "mono below",
                              "mono the low", "collapse the bass", "bass in mono", "lows in mono", "mono the lows" }))
        {
            Intent i { IntentType::MonoBass };
            i.freqHz = freq;
            i.direction = hasAny (clause, { "turn off", "disable", "stereo bass" }) ? -1.0f : 1.0f;
            i.phrase = clause;
            add (i);
            continue;
        }

        if (hasAny (clause, { "telephone", "telephonic", "radio effect", "radio", "phone", "lofi", "lo-fi", "lo fi", "megaphone", "walkie" }))
        {
            Intent i { IntentType::Telephone };
            i.intensity = intensity;
            i.phrase = clause;
            add (i);
            continue;
        }

        if (hasAny (clause, { "vintage", "retro", "old school", "old-school", "analog", "analogue" }))
        {
            Intent i { IntentType::Vintage };
            i.intensity = intensity;
            i.phrase = clause;
            add (i);
            continue;
        }

        if (mod != Module::Limiter
            && hasAny (clause, { "saturation", "saturate", "saturated", "tape", "harmonics", "drive", "grit", "gritty", "dirt", "dirty",
                                 "distortion", "distort", "crunch", "crunchy", "tube", "valve", "colour", "color", "clipper", "soft clip" }))
        {
            Intent i { IntentType::Saturate };
            i.intensity = intensity;
            i.direction = negated ? -1.0f : 1.0f;
            i.saturationType = hasAny (clause, { "tube", "valve" }) ? 1 : (hasAny (clause, { "clip", "clipper", "soft clip", "clipping" }) ? 2 : 0);
            i.gainDb = firstWithUnit (nums, { "db" });
            i.percent = firstWithUnit (nums, { "%" });
            i.phrase = clause;
            add (i);
            continue;
        }

        if (hasAny (clause, { "compress", "compression", "compressor", "compressed" }) && ! result.has (IntentType::CompSetting))
        {
            Intent i { IntentType::Glue };
            i.intensity = intensity;
            i.direction = negated ? -1.0f : 1.0f;
            i.phrase = clause;
            add (i);
            continue;
        }

        // Loudness
        const bool limiterPush = mod == Module::Limiter && hasAny (clause, { "harder", "drive", "push", "more" });
        if ((limiterPush || hasAny (clause, { "louder", "loud", "volume", "turn it up", "more level", "hotter", "quieter", "softer",
                                              "turn it down", "too loud" }))
            && ! hasAny (clause, { "vocal", "vocals", "kick", "snare", "drums", "bass", "hats", "guitar", "synth" })
            && ! result.has (IntentType::LoudnessTarget) && ! result.has (IntentType::Status))
        {
            Intent i { IntentType::Louder };
            const bool down = hasAny (clause, { "quieter", "softer", "turn it down", "too loud", "less loud", "lower the volume", "reduce the volume" });
            i.direction = down ? -1.0f : 1.0f;
            i.intensity = intensity;
            i.gainDb = firstWithUnit (nums, { "db" });
            i.phrase = clause;
            add (i);
            continue;
        }

        // Explicit EQ moves: "boost 3k by 2 dB", "cut 250 Hz"
        if (freq && hasAny (clause, { "boost", "cut", "add", "remove", "reduce", "raise", "lower", "dip", "notch", "bump", "lift",
                                      "eq", "increase", "decrease", "attenuate", "push", "pull" }))
        {
            Intent i { IntentType::EqBand };
            i.freqHz = freq;
            const bool cut = hasAny (clause, { "cut", "remove", "reduce", "lower", "dip", "notch", "decrease", "attenuate", "pull" });
            auto gain = firstWithUnit (nums, { "db" });
            if (gain)
                i.gainDb = cut ? -std::abs (*gain) : (*gain);
            i.direction = cut ? -1.0f : 1.0f;
            i.intensity = intensity;
            i.phrase = clause;
            add (i);
            continue;
        }

        // Width with explicit percentage
        if (hasAny (clause, { "width", "stereo" }) && firstWithUnit (nums, { "%" }))
        {
            Intent i { IntentType::Wider };
            i.percent = firstWithUnit (nums, { "%" });
            i.phrase = clause;
            add (i);
            continue;
        }

        // Descriptors ("warmer", "too harsh", "not enough bass", ...)
        std::vector<bool> used (std::size (descriptors), false);
        size_t pos = 0;
        std::vector<IntentType> seenTypes;

        while (const Descriptor* d = matchDescriptor (clause, pos, used))
        {
            if (std::find (seenTypes.begin(), seenTypes.end(), d->type) != seenTypes.end())
                continue;

            // "bright" inside "brightness" etc. is handled by the ordering of the lexicon;
            // avoid double-counting a type within one clause.
            seenTypes.push_back (d->type);

            Intent i { d->type };
            i.intensity = intensity;
            i.phrase = d->phrase;

            if (d->comparative)
                i.direction = d->desireDir * (negated && ! hasWord (clause, "too") ? -1.0f : 1.0f);
            else if (d->complaintDir != 0.0f && ! desire)
                i.direction = d->complaintDir;
            else
                i.direction = d->desireDir * (negated ? -1.0f : 1.0f);

            if (hasAny (clause, { "way too", "far too", "much too" }))
                i.intensity = std::max (i.intensity, 1.5f);

            i.freqHz = freq;
            add (i);
        }
    }

    const bool onlySmallTalk = std::all_of (result.intents.begin(), result.intents.end(), [] (const Intent& i)
                                            { return i.type == IntentType::Thanks || i.type == IntentType::Greeting; });
    if (onlySmallTalk)
    {
        if (applyCandidate)
            add (simple (IntentType::Apply, all));
        else if (dismissCandidate)
            add (simple (IntentType::Dismiss, all));
    }

    return result;
}

} // namespace prizm::assistant
