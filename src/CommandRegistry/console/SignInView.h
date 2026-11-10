#pragma once

// SignInView: what the Ghosty panel shows until Ghosty can be used, in place of the chat. Ghosty needs a Ghosty
// account (Account) and the Launch plan (its 7-day free trial included):
//
//   signed out           Welcome to Ghosty              [Continue with Google]
//   signing in           Continue in your browser (it comes back to LYNX by itself), Open the page again / Cancel
//   checking the plan    Checking your plan...          (Try again when the server doesn't answer)
//   no plan              Start your 7-day free trial    [See plans]   (the plans page in the browser)
//   paying               Finish the checkout in your browser   [I've paid: check now] [Stop waiting]
//
// Signed in, the footer says who, with a small "Sign out". Once the plan is on, the panel swaps this for the chat by
// itself (Account follows the payment).

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::commands
{
class SignInView final : public juce::Component, private juce::ChangeListener
{
    // a small text link, always on one line, in the footer's font ("Sign out")
    class Link final : public juce::Button
    {
    public:
        Link() : juce::Button ({}) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics& g, bool over, bool down) override;
    };

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
    juce::TextButton seePlans;                              // no plan: the plans page in the browser
    Link signOut;                                           // (in the footer, after "Signed in as ...")
    juce::TextButton checkNow, stopWaiting;                 // paying
    juce::Rectangle<int> content;                           // (the centred column)
    juce::Rectangle<int> footerTextArea;                    // (the footer's text, left of Sign out)
    juce::String footerLine;                                // "Signed in as you@example.com  ·" (shortened to fit)
};
} // namespace lynx::commands
