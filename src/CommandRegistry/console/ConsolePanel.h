#pragma once

// ConsolePanel: the Console workspace panel, a chat in full-width rows (like Claude Code). You type commands (the
// `lynx` CLI's syntax) in the composer at the bottom; each shows as a soft row marked "›", and LYNX's reply under it
// marked with a dot (red for an error), the text across the whole width:
//
//   ┌──────────────────────────────────────────┐
//   │ › tempo set --bpm 128                     │
//   └──────────────────────────────────────────┘
//   ● { "bpm": 128.0, "previous": 124.0 }
//
//   ┌──────────────────────────────────────────┐
//   │ Message LYNX...                           │
//   │ Enter to send · Shift+Enter new line  🎤 ⬆ │
//   └──────────────────────────────────────────┘
//
// The input takes several lines: Shift+Enter starts a new line, Enter (or the send button) runs every line in order
// (blank lines and # comments are skipped; a failing line stops the rest). A leading "lynx " is dropped, so lines
// pasted from the terminal work as they are. Up/Down walk the history (from the first / last line of the input), Tab
// completes a command name, Escape clears the input. Words: help, <group> --help, clear. Right-click a message to
// copy it (or, on your own, to edit it again). The mic button is a placeholder: voice input isn't wired up yet.
//
// The composer's bottom row: [⚙ Settings] [model picker ⌄] ... [mic] [send]. The picker chooses which model answers
// in the chat ("Commands" = no AI; Claude's models, and OpenAI / Google ones once their keys are in Settings); the gear
// opens the Settings view (API keys, SettingsView) in place of the chat.
//
// With a Claude or OpenAI model picked, anything that isn't a command goes to the agent (AgentClient → the agent service run by
// `bun run dev`): its words stream in as a message, and each command it runs shows as a row (✓ / ✗ when done). While
// it works the send button is a Stop button (Escape stops it too). "clear" starts a new conversation.
//
// The header (the workspace's title row, shared): no title; on the right, the plan: an orange "Start free trial" /
// "Upgrade" on the free plan (monthly or yearly), or "Launch" on it (your account menu).
//
// Until you're signed in to a LYNX account (Account), the sign-in screen (SignInView) stands in for all of it; once
// signed in, your initials sit at the left of the composer's bottom row (click: who's signed in, Account page, Sign out).
//
// Commands go straight to the CommandRegistry on the UI thread (no HTTP).

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/EditorTools.h" // (SharesTitleBar)

#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace lynx::commands
{
class AgentClient;
class SettingsView;
class SignInView;

class ConsolePanel final : public juce::Component, public SharesTitleBar, private juce::ChangeListener
{
public:
    ConsolePanel();
    ~ConsolePanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void focusGained (FocusChangeType) override; // (the panel was focused: the input takes the keys)
    void mouseDown (const juce::MouseEvent&) override;

    // the panel's title row is the Console's own header: the plan button sits in it, before the window buttons
    void setTitleBarInsets (int left, int right) override;

    // user: what you typed; reply / help / error: a command's answer; assistant: the AI's words; tool: a command the AI
    // ran; toolGroup: the row "▸ Ran 5 commands" that folds the commands run in a row (click: show / hide them)
    enum class Kind { user, reply, error, help, info, assistant, tool, toolGroup };

    // one piece of a message's text, with its own look
    struct Span
    {
        juce::String text;
        juce::Colour colour;
        bool mono = true;
        bool bold = false;
    };

private:
    class MessageList; // the conversation (scrolls in `viewport`)
    class Composer;    // the input box with the mic and send buttons

    void submit();
    bool runLine (const juce::String& line); // one command; false when it failed
    bool handleKey (const juce::KeyPress& key);
    void complete();
    void recallHistory (int delta);

    void post (Kind kind, std::vector<Span> spans);
    void postHelp (const juce::var& description, const juce::String& onlyCommand = {});
    void layoutConversation (bool scrollToEnd);

    // the model picker and Settings (the gear): AiSettings
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshModelPicker();
    void showModelMenu();
    void showSettings (bool show);

    // the LYNX account (Account): signed out, the sign-in screen stands in for the chat; signed in, the avatar's menu
    void accountChanged();
    void updateGate();
    void showAccountMenu (juce::Component& target);
    void showUpgradeMenu();
    void refreshPlanButton();

    // the agent (an AI model is picked): AgentClient → Ghosty's agent on the Ghosty server, which asks LYNX to run commands
    bool isCommandScript (const juce::String& text) const;
    void sendToAgent (const juce::String& text);
    void agentEvent (const juce::var& event);
    void runForAgent (const juce::String& callId, const juce::String& name, const juce::var& args);

    std::unique_ptr<MessageList> list;
    juce::Viewport viewport;
    std::unique_ptr<Composer> composer;
    std::unique_ptr<SettingsView> settingsView;
    bool showingSettings = false;
    std::unique_ptr<SignInView> signInView;
    bool signedIn = false;

    class PlanButton; // the header's "Start free trial" / "Upgrade" (orange) or "Launch" (outlined)
    std::unique_ptr<PlanButton> planButton;
    int titleLeft = 0, titleRight = 0;

    std::unique_ptr<AgentClient> agent;
    int assistantMessage = -1;                                       // the AI message being streamed into (-1: none)
    std::map<juce::String, std::pair<int, juce::String>> toolRows; // tool call id → (its row, the command shown)
    // the fold the agent's commands go into (-1: the next command starts a new one) and how its commands went
    int toolGroup = -1, groupCount = 0, groupDone = 0, groupFailed = 0;
    void updateToolGroup (bool finished);

    juce::StringArray history; // each entry is a whole submission (several lines when it was a script)
    int historyIndex = 0;      // == history.size(): the text being typed
    juce::String draft;        // what was typed before walking the history
};
} // namespace lynx::commands
