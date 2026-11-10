#pragma once

// SignInView: what the Console shows until you're signed in to a LYNX account (Account), in place of the chat.
//
//                 (L)
//          Welcome to LYNX
//   Sign in or create an account to start.
//      [ Create account ]  [ Sign in ]
//
// Both buttons start the browser sign-in (Account::signIn); while it waits, the view shows the code to approve, with
// "Open the page again" and Cancel. Once signed in, the Console swaps it for the chat.

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::commands
{
class SignInView final : public juce::Component, private juce::ChangeListener
{
public:
    SignInView();
    ~SignInView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    juce::TextButton create, signIn, openAgain, cancel;
    juce::Rectangle<int> content; // (the centred column)
};
} // namespace lynx::commands
