# CommandRegistry

Drive the running app from outside: no clicking through the UI.

```
lynx                                  # every group
lynx tempo                            # one group's commands
lynx tempo get                        # {"bpm": 124}
lynx tempo set --bpm 128              # {"bpm": 128, "previous": 124}
lynx playlist tracks
lynx project new --name "Sketch" --template "Basic beat"

curl localhost:7878/tempo.get
curl localhost:7878/tempo.set -H 'Content-Type: application/json' -d '{"bpm": 128}'
```

## Layout

```
CommandRegistry/
├── core/                       the framework (features never need to change it)
│   ├── CommandModule.h/.cpp    the interface every feature implements + the Context handlers get
│   ├── CommandRegistry.h/.cpp  groups, commands, argument checking, help as data
│   ├── CommandServer.h/.cpp    HTTP on 127.0.0.1:7878, runs each command on the UI thread
│   └── CommandBridge.h/.cpp    the list of modules; made in Main.cpp
├── console/ConsolePanel        the in-app Console panel (Cmd+J): types commands straight into the registry
├── shared/Lookup               helpers modules share: find a pattern by name or number, channel names, colours, paths
├── shared/Catalog              pick-by-name lists (channel instruments, mixer effects): --format, VST3 first, ids
├── tempo/TempoModule           tempo.get, tempo.set
├── project/ProjectModule       project.get, project.templates, project.new
├── channel/ChannelModule       channel.list, channel.get, channel.add, channel.replace, channel.sample, channel.delete, ...
├── sample/SampleModule         sample.folders, sample.search, sample.preview (the samples in the Browser's folders)
├── pattern/PatternModule       pattern.list, pattern.new, pattern.notes, pattern.note.add, pattern.steps
├── mixer/MixerModule           mixer.tracks, mixer.track, mixer.track.set, mixer.link, mixer.route, mixer.unroute,
│                               mixer.effects, mixer.effect.add, mixer.effect.remove, mixer.effect.set,
│                               mixer.effect.params, mixer.effect.param.set
├── transport/TransportModule   transport.get, transport.mode, transport.play, transport.stop
└── playlist/PlaylistModule     playlist.tracks, playlist.track, playlist.track.set, playlist.clip.add,
                                playlist.clip.delete, playlist.clear, playlist.markers, playlist.marker.add,
                                playlist.marker.delete
```

## The flow every command shares

```
lynx tempo set --bpm 128
  → CommandServer     HTTP request → "tempo.set" + {"bpm": "128"}; mutation? then it must be a JSON POST
  → UI thread         (the only thread allowed to touch the Song)
  → CommandRegistry   check args against the declaration: present, right type, in range, no unknown flags
  → your handler      (Context& ctx, const Args& args) → Result
  → JSON reply        {"ok": true, "command": "tempo.set", "result": {...}}
```

A module only writes the handler.

## Writing a module

A module is one group. It implements `CommandModule` (the C++ form of an interface: an abstract class) and
declares its commands in `define()` as **queries** (read) or **mutations** (change the project):

```cpp
// mixer/MixerModule.h
class MixerModule final : public CommandModule
{
public:
    MixerModule() : CommandModule ("mixer", "Mixer tracks: volume, pan, mute") {}
    void define() override;
};

// mixer/MixerModule.cpp
void MixerModule::define()
{
    query ("tracks", "Every mixer track", [] (Context& ctx, const Args&) {
        juce::Array<juce::var> list;
        for (const auto& t : ctx.song().mixer) list.add (makeObject ({ { "name", juce::String (t.name) } }));
        return Result::success (list);
    });

    mutation ("mute", "Mute or unmute a mixer track",
              { Arg::number ("track", "mixer track, 0 = Master", 0, 126), Arg::boolean ("on", "mute it") },
              [] (Context& ctx, const Args& args) {
                  const auto track = (int) args.number ("track");
                  ctx.edit ([&] (Song& s) { s.mixer[(size_t) track].muted = args.boolean ("on"); });
                  return Result::success();
              });
}
```

Then:
1. add one line to `core/CommandBridge.cpp`: `modules.push_back (std::make_unique<MixerModule>());`
2. add `src/CommandRegistry/mixer/MixerModule.cpp` to the CommandRegistry block in `CMakeLists.txt`.

(The example is a sketch; check the real `Song`/`MixerTrack` fields before using it.)

### What a handler gets

| | |
|---|---|
| `ctx.song()` | the open project, read only |
| `ctx.edit ([&] (Song& s) { ... })` | change it like the UI does: undo step, "modified", audio, repaint |
| `ctx.ui()` | the main window (`MainComponent*`), for things beyond the Song (new project, open file); `nullptr` for a moment at start-up → `return Context::windowNotReady();` |
| `ctx.engine` | the `AudioEngine` itself (transport, `isModified()`, ...) |
| `args.number ("bpm")`, `args.text (...)`, `args.boolean (...)`, `args.has (...)` | arguments, already checked |
| `Result::success (value)` / `Result::failure ("why")` | the reply |
| `makeObject ({ { "key", value }, ... })` | a JSON object in one line |

### Arguments

`Arg::number (name, description, min, max)`, `Arg::text (...)`, `Arg::boolean (...)`; add `.optional()` for
optional ones (read them with a fallback: `args.number ("bpm", song.bpm)`). Help (`lynx <group> <command> --help`)
comes from these declarations.

## Safety

- Listens on 127.0.0.1 only; requests whose `Host` or `Origin` isn't local are refused (a web page can't reach it).
- Mutations only run from a POST with a JSON body, which a browser won't send cross-site.

`LYNX_COMMAND_PORT=7979` picks another port; `0` turns the bridge off. One LYNX window owns the port at a time.
