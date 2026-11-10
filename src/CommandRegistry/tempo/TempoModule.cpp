#include "CommandRegistry/tempo/TempoModule.h"

#include "core/Song.h"

#include <cmath>

namespace lynx::commands
{
void TempoModule::define()
{
    query ("get", "The project tempo in BPM", [] (Context& ctx, const Args&) {
        return Result::success (makeObject ({ { "bpm", ctx.song().bpm } }));
    });

    mutation ("set", "Set the project tempo (one undo step, like dragging the tempo display)",
              { Arg::number ("bpm", "the new tempo in BPM", minTempo, maxTempo) },
              [] (Context& ctx, const Args& args) {
                  const auto bpm = args.number ("bpm");
                  const auto previous = ctx.song().bpm;
                  if (std::abs (bpm - previous) > 1e-9)
                      ctx.edit ([bpm] (Song& song) { song.bpm = bpm; });
                  return Result::success (makeObject ({ { "bpm", bpm }, { "previous", previous } }));
              });

    examples ({ { "get", "tempo get" },
                { "set", "tempo set --bpm 128" } });
}
} // namespace lynx::commands
