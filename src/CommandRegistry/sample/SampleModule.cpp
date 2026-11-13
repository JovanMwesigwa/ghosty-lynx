#include "CommandRegistry/sample/SampleModule.h"

#include "CommandRegistry/shared/Lookup.h"
#include "core/SampleFiles.h"
#include "core/SearchQuery.h"
#include "engine/AudioEngine.h"
#include "engine/SampleLibrary.h"
#include "ui/BrowserComponent.h"

#include <algorithm>
#include <random>

namespace lynx::commands
{
namespace
{
// ---- the folders searched: the Browser's places that hold samples (not its presets, demo projects or MIDI)

struct SampleFolder
{
    juce::String name;   // as the Browser shows it
    juce::String source; // "fl", "user", "music", "documents", "desktop", "downloads"
    juce::File folder;
    juce::String path;   // folder's full path (compared with the index's)
};

std::vector<SampleFolder> sampleFolders()
{
    using browserview::RootKind;
    // (in this order: real sample libraries first, then the places where a sample may happen to be)
    const std::pair<RootKind, const char*> order[] = { { RootKind::flSamples, "fl" }, { RootKind::user, "user" },
                                                       { RootKind::music, "music" },  { RootKind::documents, "documents" },
                                                       { RootKind::desktop, "desktop" }, { RootKind::downloads, "downloads" } };
    const auto roots = BrowserComponent::knownRoots();
    std::vector<SampleFolder> list;
    for (const auto& [kind, source] : order)
        for (const auto& r : roots)
            if (r.kind == kind)
                list.push_back ({ r.name, source, r.folder, r.folder.getFullPathName() });
    return list;
}

// The folder a path is (or is inside of): its index in `folders`, -1 when none
int folderOf (const std::vector<SampleFolder>& folders, const juce::String& path)
{
    for (int i = 0; i < (int) folders.size(); ++i)
    {
        const auto& root = folders[(size_t) i].path;
        if (path == root || (path.startsWith (root) && (path[root.length()] == '/' || path[root.length()] == '\\')))
            return i;
    }
    return -1;
}

// ---- what kind of sound a sample is, from its name and its folders' names

struct KindRule
{
    const char* kind;
    std::vector<std::string> anywhere; // found anywhere in the name ("BigKick01")
    std::vector<std::string> words;    // whole words only ("BD 01", "SD_02": too short to search inside other words)
};

// first match wins (snare before kick: "Kick & Snare" folders are rare, "808 Snare" files aren't)
const std::vector<KindRule>& kindRules()
{
    static const std::vector<KindRule> rules {
        { "snare", { "snare" }, { "sd", "snr", "sn" } },
        { "clap", { "clap" }, { "clp" } },
        { "openhat", { "openhat", "open hat", "open-hat", "open_hat" }, { "oh", "ohh", "ohat" } },
        { "hat", { "hihat", "hi-hat", "hi hat", "hi_hat", "closedhat", "closed hat" }, { "hat", "hats", "hh", "chh" } },
        { "cymbal", { "cymbal" }, { "crash", "crashes", "ride", "rides", "cym", "splash", "china" } },
        { "tom", { "tomtom", "floortom" }, { "tom", "toms" } },
        { "rim", { "rimshot" }, { "rim", "rims", "stick", "sidestick" } },
        { "shaker", { "shaker", "tambourine", "maraca", "cabasa" }, { "tamb", "shk" } },
        { "perc", { "perc", "cowbell", "conga", "bongo", "clave", "timbale", "triangle", "agogo", "guiro", "woodblock" }, { "block" } },
        { "kick", { "kick", "bassdrum", "bass drum", "bass_drum" }, { "bd", "kik", "kicks" } },
        { "808", { "808" }, { "sub", "subs", "subbass" } },
        { "vocal", { "vocal", "vox", "voice", "chant", "adlib" }, {} },
        { "fx", { "riser", "sweep", "impact", "downlifter", "uplifter", "whoosh", "reverse" }, { "fx", "sfx", "noise" } },
    };
    return rules;
}

// lower-case words: letters and digits apart ("TR808_Kick01" → tr, 808, kick, 01)
std::vector<std::string> wordsOf (const std::string& lower)
{
    std::vector<std::string> words;
    std::string current;
    int currentClass = 0; // 1 letter, 2 digit
    for (const auto ch : lower)
    {
        const auto c = (unsigned char) ch;
        const auto cls = std::isalpha (c) ? 1 : std::isdigit (c) ? 2 : 0;
        if (cls != currentClass && ! current.empty())
        {
            words.push_back (current);
            current.clear();
        }
        if (cls != 0)
            current += (char) c;
        currentClass = cls;
    }
    if (! current.empty())
        words.push_back (current);
    return words;
}

// A machine-made name (a UUID or hash, like Melodyne's transfers "AA7B590C-BD58-..."): its letters mean nothing
bool isMachineName (const std::string& lower)
{
    int hex = 0;
    for (const auto ch : lower)
    {
        if (std::isxdigit ((unsigned char) ch))
            ++hex;
        else if (ch != '-' && ch != '_' && ch != '.' && ch != ' ')
            return false;
    }
    return hex >= 16;
}

const char* kindOfText (const std::string& lower)
{
    if (isMachineName (lower))
        return nullptr;
    const auto words = wordsOf (lower);
    for (const auto& rule : kindRules())
    {
        for (const auto& a : rule.anywhere)
            if (lower.find (a) != std::string::npos)
                return rule.kind;
        for (const auto& w : rule.words)
            if (std::find (words.begin(), words.end(), w) != words.end())
                return rule.kind;
    }
    return nullptr;
}

// The file's name decides first; else its folders, the nearest first ("Drums/Kicks/Big One.wav" is a kick)
const char* kindOf (const juce::String& stem, const juce::StringArray& folders)
{
    if (const auto k = kindOfText (stem.toLowerCase().toStdString()))
        return k;
    for (int i = folders.size(); --i >= 0;)
        if (const auto k = kindOfText (folders[i].toLowerCase().toStdString()))
            return k;
    return nullptr;
}

bool isLoop (const juce::String& lowerRelativePath)
{
    if (lowerRelativePath.contains ("loop"))
        return true;
    const auto words = wordsOf (lowerRelativePath.toStdString());
    return std::find (words.begin(), words.end(), "bpm") != words.end();
}

// --kind as typed → a kind name ("kicks", "Hi-hats", "bass" → kick, hat, 808); "" when it isn't one
juce::String normaliseKind (juce::String text)
{
    text = text.trim().toLowerCase().removeCharacters (" -_");
    static const std::pair<const char*, const char*> synonyms[] = {
        { "hihat", "hat" },   { "hh", "hat" },          { "closedhat", "hat" }, { "oh", "openhat" },   { "crash", "cymbal" },
        { "ride", "cymbal" }, { "bass", "808" },        { "sub", "808" },       { "percussion", "perc" }, { "vox", "vocal" },
        { "voice", "vocal" }, { "rimshot", "rim" },     { "bd", "kick" },       { "sd", "snare" },    { "effect", "fx" },
        { "loops", "loop" },  { "tambourine", "shaker" } };
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (text == "loop")
            return text;
        for (const auto& rule : kindRules())
            if (text == rule.kind)
                return text;
        for (const auto& [from, to] : synonyms)
            if (text == from)
                return to;
        if (! text.endsWith ("s"))
            break;
        text = text.dropLastCharacters (1); // kicks → kick, hats → hat
    }
    return {};
}

juce::String kindList()
{
    juce::StringArray kinds;
    for (const auto& rule : kindRules())
        kinds.add (rule.kind);
    kinds.add ("loop");
    return kinds.joinIntoString (", ");
}

// ---- the search's words: the Browser's search language, matched against the path below the folder (folders + file)

bool matchesQuery (const search::Query& query, const std::string& lowerRelativePath, const std::string& lowerFileName)
{
    bool anyGroup = false;
    for (const auto& group : query)
    {
        if (group.empty())
            continue;
        anyGroup = true;
        bool all = true;
        for (const auto& term : group)
        {
            // (a wildcard like kick*.wav describes the whole file name; words may be in any folder's name too)
            const auto hit = term.wildcard ? search::wildcardMatch (term.text, lowerFileName) : lowerRelativePath.find (term.text) != std::string::npos;
            if (hit == term.negated)
            {
                all = false;
                break;
            }
        }
        if (all)
            return true;
    }
    return ! anyGroup;
}

struct Found
{
    int folder = 0;
    juce::String path, stem, pack;
    const char* kind = nullptr;
    bool loop = false;
};

bool isPlayableSample (const juce::String& fileName)
{
    const auto extension = fileName.fromLastOccurrenceOf (".", false, false);
    // (.speech files are speech synthesizer scripts: not a drum sound to swap in)
    return fileName.containsChar ('.') && ! extension.equalsIgnoreCase ("speech") && isSampleExtension (extension.toStdString());
}
} // namespace

void SampleModule::define()
{
    query ("folders", "The folders samples are searched in (the Browser's: FL's Packs, your added folders, Music, Downloads...) and how many samples each holds",
           [] (Context&, const Args&) {
               const auto folders = sampleFolders();
               std::vector<int> counts (folders.size(), 0);
               juce::Array<juce::File> roots;
               for (const auto& f : folders)
                   roots.add (f.folder);
               bool indexing = false;
               BrowserComponent::visitIndexedFiles (roots, [&] (const juce::String& folder, const juce::String& name) {
                   if (isPlayableSample (name))
                       if (const auto i = folderOf (folders, folder); i >= 0)
                           ++counts[(size_t) i];
                   return true;
               }, indexing);

               juce::Array<juce::var> list;
               for (size_t i = 0; i < folders.size(); ++i)
                   list.add (makeObject ({ { "name", folders[i].name },
                                           { "source", folders[i].source },
                                           { "path", folders[i].path },
                                           { "samples", counts[i] } }));
               auto result = makeObject ({ { "folders", list } });
               if (indexing)
                   result.getDynamicObject()->setProperty ("indexing", "some folders are still being indexed: counts grow in a moment");
               return Result::success (result);
           });

    query ("search", "Find samples by kind (kick, snare, hat...) and/or words, in the Browser's folders: name, path, kind; give a path to channel sample",
           { Arg::text ("kind", "the sound: " + kindList() + " (one-shots leave out loops unless --kind loop)").optional(),
             Arg::text ("query", "words in the file or folder names, as the Browser's search: punchy, \"808 OR sub\", -loop, kick*.wav").optional(),
             Arg::text ("folder", "only one folder: a name from sample folders, or a path inside one").optional(),
             Arg::number ("limit", "how many to list (default 20)", 1, 100).optional(),
             Arg::boolean ("random", "a random pick of the matches instead of the first ones (for \"another kick\")").optional() },
           [] (Context&, const Args& args) {
               auto folders = sampleFolders();
               if (folders.empty())
                   return Result::failure ("no sample folders: add one to the Browser (its + button), or put samples in Music");

               // --folder: one of them by name, or a folder inside one
               juce::Array<juce::File> roots;
               if (const auto wanted = args.text ("folder").trim(); wanted.isNotEmpty())
               {
                   for (const auto& f : folders)
                       if (f.name.equalsIgnoreCase (wanted) || f.source.equalsIgnoreCase (wanted))
                           roots.add (f.folder);
                   if (roots.isEmpty())
                   {
                       const auto path = pathFromArg (wanted);
                       if (path == juce::File() || ! path.isDirectory())
                           return Result::failure ("--folder: \"" + wanted + "\" isn't a sample folder's name (sample folders lists them) or an existing folder");
                       if (folderOf (folders, path.getFullPathName()) < 0)
                           return Result::failure ("--folder: " + path.getFullPathName()
                                                   + " isn't inside the Browser's folders: add it to the Browser first (its + button)");
                       roots.add (path);
                   }
               }
               else
               {
                   for (const auto& f : folders)
                       roots.add (f.folder);
               }

               juce::String kind;
               if (args.has ("kind"))
               {
                   kind = normaliseKind (args.text ("kind"));
                   if (kind.isEmpty())
                       return Result::failure ("--kind: \"" + args.text ("kind") + "\" isn't one of " + kindList());
               }
               const auto query = search::parse (args.text ("query").toLowerCase().toStdString());

               std::vector<Found> found;
               bool indexing = false;
               BrowserComponent::visitIndexedFiles (roots, [&] (const juce::String& folder, const juce::String& name) {
                   if (! isPlayableSample (name))
                       return true;
                   const auto root = folderOf (folders, folder);
                   if (root < 0)
                       return true;
                   const auto pack = folder.substring (folders[(size_t) root].path.length()).trimCharactersAtStart ("/\\");
                   const auto relative = (pack.isEmpty() ? name : pack + "/" + name).toLowerCase();
                   if (! matchesQuery (query, relative.toStdString(), name.toLowerCase().toStdString()))
                       return true;

                   Found f;
                   f.folder = root;
                   f.stem = name.upToLastOccurrenceOf (".", false, false);
                   f.pack = pack;
                   f.loop = isLoop (relative);
                   f.kind = kindOf (f.stem, juce::StringArray::fromTokens (pack, "/\\", {}));
                   if (kind == "loop" ? ! f.loop : kind.isNotEmpty() && (f.loop || f.kind == nullptr || kind != f.kind))
                       return true;
                   f.path = juce::File (folder).getChildFile (name).getFullPathName();
                   found.push_back (std::move (f));
                   return true;
               }, indexing);

               // the libraries first (the folder order), then by path; --random: any of them
               std::stable_sort (found.begin(), found.end(), [] (const Found& a, const Found& b) {
                   return a.folder != b.folder ? a.folder < b.folder : a.path.compareNatural (b.path) < 0;
               });
               if (args.boolean ("random", false))
               {
                   std::mt19937 shuffle ((std::mt19937::result_type) juce::Random::getSystemRandom().nextInt64());
                   std::shuffle (found.begin(), found.end(), shuffle);
               }

               const auto limit = (size_t) args.number ("limit", 20);
               juce::Array<juce::var> list;
               for (size_t i = 0; i < found.size() && i < limit; ++i)
               {
                   const auto& f = found[i];
                   list.add (makeObject ({ { "name", f.stem },
                                           { "path", f.path },
                                           { "kind", f.kind != nullptr ? juce::var (juce::String (f.kind)) : juce::var() },
                                           { "loop", f.loop },
                                           { "folder", folders[(size_t) f.folder].name },
                                           { "pack", f.pack.isEmpty() ? juce::var() : juce::var (f.pack) } }));
               }

               auto result = makeObject ({ { "matches", (int) found.size() }, { "samples", list } });
               auto* o = result.getDynamicObject();
               if (indexing)
                   o->setProperty ("indexing", "some folders are still being indexed: search again in a moment for more");
               if (found.empty())
                   o->setProperty ("note", kind.isNotEmpty() && args.has ("query")
                                               ? "nothing matched both: try --kind or --query alone"
                                               : "nothing matched: sample folders shows where LYNX looks");
               return Result::success (result);
           });

    action ("preview", "Play a sample in the Browser's preview, to hear it before using it (no --path: stop the preview)",
            { Arg::text ("path", "the sample's path (absolute or ~/...), from sample search").optional() },
            [] (Context& ctx, const Args& args) {
                if (! args.has ("path"))
                {
                    ctx.engine.previewFile (juce::File());
                    return Result::success (makeObject ({ { "playing", juce::var() } }));
                }
                const auto file = pathFromArg (args.text ("path"));
                if (file == juce::File())
                    return Result::failure ("--path must be an absolute path or start with ~/ (got \"" + args.text ("path") + "\")");
                if (! file.existsAsFile())
                    return Result::failure ("no file at " + file.getFullPathName());
                if (! SampleLibrary::isSampleFile (file))
                    return Result::failure (file.getFileName() + " isn't an audio file LYNX plays (WAV, AIFF, FLAC, OGG, MP3, WV)");
                ctx.engine.previewFile (file);
                return Result::success (makeObject ({ { "playing", file.getFullPathName() } }));
            });

    examples ({ { "folders", "sample folders" },
                { "search", "sample search --kind kick --random" },
                { "preview", "sample preview --path \"~/Music/Samples/Kick 01.wav\"" } });
}
} // namespace lynx::commands
