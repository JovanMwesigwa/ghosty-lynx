#include "CommandRegistry/project/ProjectModule.h"

#include "CommandRegistry/shared/Lookup.h"
#include "core/Templates.h"
#include "engine/AudioEngine.h"
#include "engine/ProjectFile.h"
#include "platform/Paths.h"
#include "ui/MainComponent.h"
#include "ui/NewProjectDialog.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace lynx::commands
{
namespace
{
// A template, as the New project window lists them: LYNX's default, the built-in ones, then the user's .lynx files.
struct TemplateChoice
{
    juce::String name;
    juce::String kind;   // "built-in" or "user"
    int builtIn = -1;    // makeNewProject's index (-1 = the default song)
    juce::File file;     // a user template
};

std::vector<TemplateChoice> templateChoices()
{
    std::vector<TemplateChoice> all { { "Default", "built-in", -1, {} } };
    const auto names = templateNames();
    for (int i = 0; i < (int) names.size(); ++i)
        all.push_back ({ juce::String::fromUTF8 (names[(std::size_t) i].c_str()), "built-in", i, {} });
    for (const auto& f : userTemplatesFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + ProjectFile::extension))
        all.push_back ({ juce::String::fromUTF8 (templateDisplayName (f.getFileName().toStdString()).c_str()), "user", -1, f });
    return all;
}

// The song a template starts with (the same steps as the window's makeSong).
std::optional<Song> songFromTemplate (const TemplateChoice& choice, juce::String& error)
{
    if (choice.file == juce::File())
        return makeNewProject (choice.builtIn);
    return ProjectFile::load (choice.file, error);
}

// "~/Music" → the home folder's Music; relative paths are refused (relative to what? the app's working folder)
std::optional<juce::File> folderFromArg (const juce::String& text)
{
    auto path = text.trim();
    if (path == "~" || path.startsWith ("~/"))
        path = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName() + path.substring (1);
    if (! juce::File::isAbsolutePath (path))
        return std::nullopt;
    return juce::File (path);
}

juce::var describeProject (const Context& ctx, const MainComponent& ui)
{
    const auto& song = ctx.song();
    const auto& file = ui.getCurrentFile();
    return makeObject ({
        { "name", file == juce::File() ? juce::var() : juce::var (file.getFileNameWithoutExtension()) },
        { "file", file == juce::File() ? juce::var() : juce::var (file.getFullPathName()) },
        { "unsavedChanges", ctx.engine.isModified() },
        { "bpm", song.bpm },
        { "beatsPerBar", song.beatsPerBar },
        { "title", juce::String::fromUTF8 (song.info.title.c_str()) },
        { "author", juce::String::fromUTF8 (song.info.author.c_str()) },
        { "genre", juce::String::fromUTF8 (song.info.genre.c_str()) },
    });
}

// ---- the projects project.list shows and project.switch picks from ----------------------------------------------

// Where projects live: the folder our commands use, the New project window's default, and an optional extra one.
std::vector<juce::File> projectFolders (const juce::String& extra)
{
    std::vector<juce::File> folders { juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Music/LYNX"),
                                      platform::documentsFolder().getChildFile ("LYNX Projects") };
    if (extra.isNotEmpty())
        if (const auto folder = pathFromArg (extra); folder != juce::File())
            folders.push_back (folder);
    return folders;
}

struct ProjectEntry
{
    juce::File file;
    bool recent = false; // in File › Recent projects
};

// The recent projects first (most recent first), then the .lynx files in the project folders (newest first): the
// files right in a folder and one level down (<folder>/<name>/<name>.lynx, as project.new saves them). Files that
// are gone are left out; each file once.
std::vector<ProjectEntry> availableProjects (const MainComponent& ui, const juce::String& extraFolder)
{
    std::vector<ProjectEntry> out;
    const auto listed = [&out] (const juce::File& f) {
        return std::any_of (out.begin(), out.end(), [&f] (const ProjectEntry& e) { return e.file == f; });
    };

    const auto& recent = ui.getRecentFiles();
    for (int i = 0; i < recent.getNumFiles(); ++i)
        if (const auto f = recent.getFile (i); f.existsAsFile() && f.hasFileExtension ("lynx;flp;mid;midi") && ! listed (f))
            out.push_back ({ f, true });

    juce::Array<juce::File> found;
    for (const auto& folder : projectFolders (extraFolder))
    {
        if (! folder.isDirectory())
            continue;
        found.addArray (folder.findChildFiles (juce::File::findFiles, false, "*.lynx"));
        for (const auto& sub : folder.findChildFiles (juce::File::findDirectories, false))
            if (! sub.isHidden())
                found.addArray (sub.findChildFiles (juce::File::findFiles, false, "*.lynx"));
    }
    std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });
    for (const auto& f : found)
        if (! listed (f))
            out.push_back ({ f, false });
    return out;
}

// "--project 3" (its number in project.list) or "--project Eastside" (its name): its place in `projects`, or -1.
int findProject (const std::vector<ProjectEntry>& projects, const juce::String& text, juce::String& error)
{
    const auto wanted = text.trim();
    if (wanted.isNotEmpty() && wanted.containsOnly ("0123456789"))
    {
        const auto n = wanted.getIntValue();
        if (n >= 1 && n <= (int) projects.size())
            return n - 1;
        error = "there's no project " + wanted + " (project list shows " + juce::String ((int) projects.size()) + ")";
        return -1;
    }

    juce::StringArray numbers;
    int found = -1;
    for (int i = 0; i < (int) projects.size(); ++i)
        if (projects[(std::size_t) i].file.getFileNameWithoutExtension().equalsIgnoreCase (wanted))
        {
            numbers.add (juce::String (i + 1));
            found = i;
        }
    if (numbers.size() > 1)
    {
        error = "several projects are named \"" + wanted + "\" (numbers " + numbers.joinIntoString (", ") + " in project list): use its number";
        return -1;
    }
    if (found < 0)
        error = "no project named \"" + wanted + "\" (project list shows them)";
    return found;
}

// Opens `file` the way File › Open does, after the checks a dialog would make: what it is, unsaved changes in the
// open project (--save saves them first, --discard drops them), a .lynx that can't be read.
Result openProject (Context& ctx, MainComponent& ui, const juce::File& file, const Args& args)
{
    if (! file.existsAsFile())
        return Result::failure ("no file at " + file.getFullPathName());
    if (! file.hasFileExtension ("lynx;flp;mid;midi"))
        return Result::failure (file.getFileName() + " isn't a project: open a .lynx, .flp or .mid file");

    if (file == ui.getCurrentFile() && ! ctx.engine.isModified())
    {
        auto result = describeProject (ctx, ui);
        result.getDynamicObject()->setProperty ("note", "it's already the open project");
        return Result::success (result);
    }

    // the window asks "save changes?" first; a command can't answer a dialog, so it must be told
    if (ctx.engine.isModified())
    {
        if (args.boolean ("save"))
        {
            juce::String error;
            if (ui.saveFromCommand ({}, error) == juce::File())
                return Result::failure ("couldn't save the open project first: " + error);
        }
        else if (! args.boolean ("discard"))
        {
            return Result::failure ("the open project has unsaved changes: pass --save to save them first, or --discard to drop them");
        }
    }

    // a .lynx that can't be read: the error here, not in a dialog
    if (file.hasFileExtension (ProjectFile::extension))
    {
        juce::String error;
        if (! ProjectFile::load (file, error))
            return Result::failure ("couldn't open " + file.getFileName() + ": " + error);
    }

    ui.openFile (file);

    // an .flp is imported in the background (behind a progress panel): it isn't open yet
    if (file.hasFileExtension ("flp"))
        return Result::success (makeObject ({ { "loading", true },
                                              { "file", file.getFullPathName() },
                                              { "note", "FL Studio projects import in the background: project get shows it once loaded" } }));
    return Result::success (describeProject (ctx, ui));
}

const auto saveArg = [] { return Arg::boolean ("save", "save the open project's unsaved changes first").optional(); };
const auto discardArg = [] { return Arg::boolean ("discard", "drop the open project's unsaved changes").optional(); };
} // namespace

void ProjectModule::define()
{
    query ("get", "The open project: name, file, unsaved changes, tempo, credits", [] (Context& ctx, const Args&) {
        const auto* ui = ctx.ui();
        if (ui == nullptr)
            return Context::windowNotReady();
        return Result::success (describeProject (ctx, *ui));
    });

    mutation ("save", "Save the project (File > Save); --file saves it as a new file (File > Save as)",
              { Arg::text ("file", "where to save it (absolute or ~/..., .lynx added); default: the project's own file").optional(),
                Arg::boolean ("overwrite", "replace another project that's already at --file").optional() },
              [] (Context& ctx, const Args& args) {
                  auto* ui = ctx.ui();
                  if (ui == nullptr)
                      return Context::windowNotReady();

                  juce::File target;
                  if (args.has ("file"))
                  {
                      target = pathFromArg (args.text ("file"));
                      if (target == juce::File())
                          return Result::failure ("--file must be an absolute path or start with ~/ (got \"" + args.text ("file") + "\")");
                      if (! target.hasFileExtension (ProjectFile::extension))
                          target = target.withFileExtension (ProjectFile::extension);
                      // the window asks before replacing a file; a command must be told
                      if (target.existsAsFile() && target != ui->getCurrentFile() && ! args.boolean ("overwrite"))
                          return Result::failure (target.getFullPathName() + " already exists: pass --overwrite to replace it");
                  }

                  juce::String error;
                  const auto written = ui->saveFromCommand (target, error);
                  if (written == juce::File())
                      return Result::failure (error);

                  auto result = describeProject (ctx, *ui);
                  result.getDynamicObject()->setProperty ("saved", written.getFullPathName());
                  return Result::success (result);
              });

    mutation ("open", "Open a project: .lynx, an FL Studio .flp (imported) or a MIDI file (File > Open)",
              { Arg::text ("file", "the project to open (absolute or ~/...)"), saveArg(), discardArg() },
              [] (Context& ctx, const Args& args) {
                  auto* ui = ctx.ui();
                  if (ui == nullptr)
                      return Context::windowNotReady();
                  const auto file = pathFromArg (args.text ("file"));
                  if (file == juce::File())
                      return Result::failure ("--file must be an absolute path or start with ~/ (got \"" + args.text ("file") + "\")");
                  return openProject (ctx, *ui, file, args);
              });

    query ("list", "The projects you can switch to: recent ones first, then the .lynx files in your project folders",
           { Arg::text ("folder", "also look in this folder (absolute or ~/...); always: ~/Music/LYNX and Documents/LYNX Projects").optional() },
           [] (Context& ctx, const Args& args) {
               const auto* ui = ctx.ui();
               if (ui == nullptr)
                   return Context::windowNotReady();

               const auto projects = availableProjects (*ui, args.text ("folder"));
               juce::Array<juce::var> list;
               for (int i = 0; i < (int) projects.size(); ++i)
               {
                   const auto& p = projects[(std::size_t) i];
                   list.add (makeObject ({ { "project", i + 1 },
                                           { "name", p.file.getFileNameWithoutExtension() },
                                           { "file", p.file.getFullPathName() },
                                           { "modified", p.file.getLastModificationTime().formatted ("%Y-%m-%d %H:%M") },
                                           { "recent", p.recent },
                                           { "current", p.file == ui->getCurrentFile() } }));
               }
               juce::Array<juce::var> folders;
               for (const auto& f : projectFolders (args.text ("folder")))
                   folders.add (f.getFullPathName());
               return Result::success (makeObject ({ { "projects", list }, { "searched", folders } }));
           });

    mutation ("switch", "Switch to another project from project list, by its number or name",
              { Arg::text ("project", "its number or name in project list"),
                Arg::text ("folder", "the same extra folder you gave project list, if any").optional(),
                saveArg(),
                discardArg() },
              [] (Context& ctx, const Args& args) {
                  auto* ui = ctx.ui();
                  if (ui == nullptr)
                      return Context::windowNotReady();

                  const auto projects = availableProjects (*ui, args.text ("folder"));
                  juce::String error;
                  const auto index = findProject (projects, args.text ("project"), error);
                  if (index < 0)
                      return Result::failure (error);
                  return openProject (ctx, *ui, projects[(std::size_t) index].file, args);
              });

    query ("templates", "The templates project.new --template accepts", [] (Context&, const Args&) {
        juce::Array<juce::var> list;
        for (const auto& t : templateChoices())
            list.add (makeObject ({ { "name", t.name }, { "kind", t.kind } }));
        return Result::success (list);
    });

    mutation ("new", "Start a new project (like File > New); saved at once when --folder is given",
              { Arg::text ("name", "the project's name (its file name); default \"Untitled\"").optional(),
                Arg::text ("template", "a name from project.templates; default \"Default\"").optional(),
                Arg::text ("folder", "save it in this folder (absolute or ~/...); omit to leave it unsaved").optional(),
                Arg::boolean ("ownFolder", "make <folder>/<name>/ for it (default true)").optional(),
                Arg::number ("bpm", "tempo; default: the template's", minTempo, maxTempo).optional(),
                Arg::number ("beatsPerBar", "time signature's beats per bar; default: the template's", 1, 16).optional(),
                Arg::text ("title", "credits: title (default: the name)").optional(),
                Arg::text ("author", "credits: author").optional(),
                Arg::text ("genre", "credits: genre").optional(),
                Arg::text ("url", "credits: link").optional(),
                Arg::boolean ("discard", "drop the open project's unsaved changes").optional() },
              [] (Context& ctx, const Args& args) {
                  auto* ui = ctx.ui();
                  if (ui == nullptr)
                      return Context::windowNotReady();

                  // The window asks "save changes?" here; a command can't answer a dialog, so it must say so.
                  if (ctx.engine.isModified() && ! args.boolean ("discard"))
                      return Result::failure ("the open project has unsaved changes: save it first, or pass --discard to drop them");

                  // ---- the template
                  const auto wanted = args.text ("template", "Default").trim();
                  const auto choices = templateChoices();
                  const auto choice = std::find_if (choices.begin(), choices.end(),
                                                    [&] (const TemplateChoice& t) { return t.name.equalsIgnoreCase (wanted); });
                  if (choice == choices.end())
                  {
                      juce::StringArray names;
                      for (const auto& t : choices)
                          names.add (t.name);
                      return Result::failure ("no template \"" + wanted + "\" (there are: " + names.joinIntoString (", ") + ")");
                  }
                  juce::String error;
                  auto song = songFromTemplate (*choice, error);
                  if (! song)
                      return Result::failure ("couldn't open the template \"" + choice->name + "\": " + error);

                  // ---- the window's fields; the template's own tempo and meter unless given
                  NewProjectSetup setup;
                  setup.name = args.text ("name").trim().toStdString();
                  setup.ownFolder = args.boolean ("ownFolder", true);
                  setup.bpm = args.number ("bpm", song->bpm);
                  setup.beatsPerBar = (int) std::lround (args.number ("beatsPerBar", song->beatsPerBar));
                  setup.info.title = args.text ("title").trim().toStdString();
                  setup.info.author = args.text ("author").trim().toStdString();
                  setup.info.genre = args.text ("genre").trim().toStdString();
                  setup.info.url = args.text ("url").trim().toStdString();
                  if (args.has ("folder"))
                  {
                      const auto folder = folderFromArg (args.text ("folder"));
                      if (! folder)
                          return Result::failure ("--folder must be an absolute path or start with ~/ (got \"" + args.text ("folder") + "\")");
                      setup.folder = folder->getFullPathName().toStdString();
                  }

                  applySetup (*song, setup);
                  if (setup.info.title.empty() && setup.name.empty()) // (as the window: no name, no title)
                      song->info.title.clear();

                  // ---- where it is saved (nowhere without --folder)
                  juce::File file;
                  if (const auto path = newProjectPath (setup, ProjectFile::extension); ! path.empty())
                  {
                      file = juce::File (juce::String::fromUTF8 (path.c_str()));
                      if (file.existsAsFile())
                          return Result::failure (file.getFullPathName() + " already exists: choose another --name or --folder");
                  }

                  // a new project replaces the whole song (not one undo step), so it goes through the window's own path
                  ui->startNewProjectFromCommand (std::move (*song), file);

                  auto result = describeProject (ctx, *ui);
                  result.getDynamicObject()->setProperty ("template", choice->name);
                  result.getDynamicObject()->setProperty ("saved", file != juce::File() && file.existsAsFile());
                  return Result::success (result);
              });

    examples ({ { "get", "project get" },
                { "templates", "project templates" },
                { "new", "project new --name \"Night Drive\" --template \"Basic beat\" --folder ~/Music/LYNX" },
                { "save", "project save --file ~/Music/LYNX/MySong.lynx" },
                { "open", "project open --file ~/Music/LYNX/MySong.lynx --save" },
                { "list", "project list" },
                { "switch", "project switch --project Eastside --save" } });
}
} // namespace lynx::commands
