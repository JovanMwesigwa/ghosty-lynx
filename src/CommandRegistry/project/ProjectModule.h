#pragma once

// project:
//   project.get         → the open project: name, file, unsaved changes, tempo, time signature, credits
//   project.templates   → the templates project.new accepts (built-in and the user's own)
//   project.list        → the projects you can switch to: File › Recent projects first, then the .lynx files in
//                         ~/Music/LYNX and Documents/LYNX Projects (and one level down); --folder adds a folder
//   project.switch --project 2|"Eastside" [--save | --discard]
//                       → opens one from that list (by number or name)
//   project.save [--file ~/Music/LYNX/Song.lynx] [--overwrite]   → File › Save / Save as
//   project.open --file ~/Music/LYNX/Song.lynx [--save | --discard] → File › Open (.lynx, .flp, .mid)
//                         (--save saves the open project's unsaved changes first, --discard drops them)
//   project.new         → a new project, like File › New's window:
//                           --name "My song"  --template "Basic beat"  --folder ~/Music/LYNX  [--ownFolder false]
//                           --bpm 128  --beatsPerBar 3  --title/--author/--genre/--url "..."
//                           --discard   (drop the open project's unsaved changes; refused without it)
//                         With --folder the project is saved at once (<folder>/<name>/<name>.lynx); without it, it
//                         stays unsaved until File › Save.

#include "CommandRegistry/core/CommandModule.h"

namespace lynx::commands
{
class ProjectModule final : public CommandModule
{
public:
    ProjectModule() : CommandModule ("project", "The open project: create, inspect") {}
    void define() override;
};
} // namespace lynx::commands
