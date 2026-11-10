#pragma once

// SignInView: what the Ghosty panel shows until Ghosty can be used, in place of the chat. Ghosty needs a Ghosty
// account (Account) and the Launch plan (its 7-day free trial included):
//
//   signed out           Welcome to Ghosty              [Create account] [Sign in]
//   signing in           the code to approve in the browser, Open the page again / Cancel
//   checking the plan    Checking your plan...          (Try again when the server doesn't answer)
//   no plan              Start your 7-day free trial    [Monthly · $50 / month] [Yearly · $490 / year]
//                        Compare plans · Sign out
//   paying               Finish the checkout in your browser   [I've paid: check now] [Stop waiting]
//
// Once the plan is on, the panel swaps this for the chat by itself (Account follows the payment).

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
    enum class Screen { signIn, starting, waiting, checking, plan, paying };
    static Screen screen();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    juce::TextButton create, signIn, openAgain, cancel;     // signing in
    juce::TextButton retry;                                 // checking the plan
    juce::TextButton monthly, yearly, compare, signOut;     // no plan
    juce::TextButton checkNow, stopWaiting;                 // paying
    juce::Rectangle<int> content;                           // (the centred column)
};
} // namespace lynx::commands
