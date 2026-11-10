#include "CommandRegistry/transport/TransportModule.h"

#include "core/Song.h"
#include "engine/AudioEngine.h"
#include "ui/MainComponent.h"

namespace lynx::commands
{
namespace
{
juce::var state (Context& ctx)
{
    return makeObject ({ { "playing", ctx.engine.getTransport().playing.load() },
                         { "mode", ctx.song().mode == PlayMode::song ? "song" : "pattern" } });
}

// --mode song|pattern → the song's play mode; false with `error` for anything else
bool setMode (Context& ctx, const juce::String& text, juce::String& error)
{
    const auto wanted = text.trim().toLowerCase();
    if (wanted != "song" && wanted != "pattern")
    {
        error = "--mode: \"" + text.trim() + "\" isn't song or pattern";
        return false;
    }
    const auto mode = wanted == "song" ? PlayMode::song : PlayMode::pattern;
    // (as the toolbar's PAT / SONG buttons: not an undo step)
    if (ctx.song().mode != mode)
        ctx.engine.editSong ([mode] (Song& s) { s.mode = mode; }, false);
    return true;
}
} // namespace

void TransportModule::define()
{
    query ("get", "Whether it's playing, and what plays (pattern or song)", [] (Context& ctx, const Args&) {
        return Result::success (state (ctx));
    });

    mutation ("mode", "What Play plays: song (the Playlist's arrangement) or pattern (the current pattern, looped)",
              { Arg::text ("mode", "song or pattern") },
              [] (Context& ctx, const Args& args) {
                  juce::String error;
                  if (! setMode (ctx, args.text ("mode"), error))
                      return Result::failure (error);
                  return Result::success (state (ctx));
              });

    mutation ("play", "Start playing from the playhead (the toolbar's Play)",
              { Arg::text ("mode", "switch to song or pattern first").optional() },
              [] (Context& ctx, const Args& args) {
                  auto* ui = ctx.ui();
                  if (ui == nullptr)
                      return Context::windowNotReady();
                  juce::String error;
                  if (args.has ("mode") && ! setMode (ctx, args.text ("mode"), error))
                      return Result::failure (error);
                  ui->playFromCommand();
                  return Result::success (state (ctx));
              });

    mutation ("stop", "Stop, and go back to the start", {}, [] (Context& ctx, const Args&) {
        auto* ui = ctx.ui();
        if (ui == nullptr)
            return Context::windowNotReady();
        ui->stopFromCommand();
        return Result::success (state (ctx));
    });

    examples ({ { "get", "transport get" },
                { "mode", "transport mode --mode song" },
                { "play", "transport play --mode song" },
                { "stop", "transport stop" } });
}
} // namespace lynx::commands
