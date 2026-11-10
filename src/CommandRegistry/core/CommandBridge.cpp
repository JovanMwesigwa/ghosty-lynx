#include "CommandRegistry/core/CommandBridge.h"

#include "CommandRegistry/channel/ChannelModule.h"
#include "CommandRegistry/mixer/MixerModule.h"
#include "CommandRegistry/pattern/PatternModule.h"
#include "CommandRegistry/playlist/PlaylistModule.h"
#include "CommandRegistry/project/ProjectModule.h"
#include "CommandRegistry/settings/SettingsModule.h"
#include "CommandRegistry/account/AccountModule.h"
#include "CommandRegistry/tempo/TempoModule.h"
#include "CommandRegistry/transport/TransportModule.h"

namespace lynx::commands
{
namespace
{
CommandRegistry* active = nullptr;
}

CommandRegistry* CommandBridge::activeRegistry() noexcept
{
    return active;
}

CommandBridge::CommandBridge (AudioEngine& engine, std::function<MainComponent*()> mainUi)
    : context (engine, std::move (mainUi))
{
    active = &registry;

    // ---- every module: one line each
    modules.push_back (std::make_unique<AccountModule>());
    modules.push_back (std::make_unique<ChannelModule>());
    modules.push_back (std::make_unique<MixerModule>());
    modules.push_back (std::make_unique<PatternModule>());
    modules.push_back (std::make_unique<PlaylistModule>());
    modules.push_back (std::make_unique<ProjectModule>());
    modules.push_back (std::make_unique<SettingsModule>());
    modules.push_back (std::make_unique<TempoModule>());
    modules.push_back (std::make_unique<TransportModule>());

    for (auto& module : modules)
        module->install (registry, context);

    const auto portText = juce::SystemStats::getEnvironmentVariable ("LYNX_COMMAND_PORT", "7878");
    if (const auto port = portText.getIntValue(); port > 0)
        server = std::make_unique<CommandServer> (registry, port);
}

CommandBridge::~CommandBridge()
{
    if (active == &registry)
        active = nullptr;
}
} // namespace lynx::commands
