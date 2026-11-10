#include "CommandRegistry/console/SignInView.h"

#include "CommandRegistry/console/Account.h"
#include "ui/GhostyLogo.h"
#include "ui/Theme.h"

namespace lynx::commands
{
namespace
{
juce::Font textFont (float size, bool bold = false) { return juce::Font (theme::font (size, bold)); }

void styleButton (juce::TextButton& b, bool filled)
{
    using namespace theme;
    b.setColour (juce::TextButton::buttonColourId, filled ? colour::accent : colour::bg2);
    b.setColour (juce::TextButton::buttonOnColourId, filled ? colour::accentPressed : colour::bg3);
    b.setColour (juce::TextButton::textColourOffId, filled ? juce::Colours::white : colour::textPrimary);
    b.setColour (juce::TextButton::textColourOnId, filled ? juce::Colours::white : colour::textPrimary);
    b.setColour (juce::ComboBox::outlineColourId, colour::bg4);
    b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

// a quiet text link ("Compare plans", "Sign out")
void styleLink (juce::TextButton& b)
{
    using namespace theme;
    b.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    b.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    b.setColour (juce::TextButton::textColourOffId, colour::textSecondary);
    b.setColour (juce::TextButton::textColourOnId, colour::textPrimary);
    b.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

// centred text (wraps to `lines`); returns the height used
int line (juce::Graphics& g, const juce::String& text, juce::Rectangle<int>& area, int height, juce::Font font, juce::Colour colour, int lines = 2)
{
    g.setColour (colour);
    g.setFont (font);
    g.drawFittedText (text, area.removeFromTop (height), juce::Justification::centred, lines);
    return height;
}

juce::String offerLabel (const juce::String& interval)
{
    for (const auto& o : Account::get().offers())
        if (o.interval == interval)
            return o.label;
    return {};
}
} // namespace

SignInView::Screen SignInView::screen()
{
    const auto& a = Account::get();
    switch (a.state())
    {
        case Account::State::signedOut: return Screen::signIn;
        case Account::State::starting: return Screen::starting;
        case Account::State::waiting: return Screen::waiting;
        case Account::State::signedIn: break;
    }
    if (a.upgrading())
        return Screen::paying;
    return a.planKnown() ? Screen::plan : Screen::checking;
}

SignInView::SignInView()
{
    create.setButtonText ("Create account");
    signIn.setButtonText ("Sign in");
    openAgain.setButtonText ("Open the page again");
    cancel.setButtonText ("Cancel");
    retry.setButtonText ("Try again");
    compare.setButtonText ("Compare plans");
    signOut.setButtonText ("Sign out");
    checkNow.setButtonText ("I've paid: check now");
    stopWaiting.setButtonText ("Stop waiting");
    for (auto* b : { &create, &monthly, &yearly, &checkNow })
        styleButton (*b, b != &monthly); // (yearly is the one to pick: the best price)
    for (auto* b : { &signIn, &openAgain, &cancel, &retry, &stopWaiting })
        styleButton (*b, false);
    for (auto* b : { &compare, &signOut })
        styleLink (*b);

    create.onClick = [] { Account::get().signIn (true); };
    signIn.onClick = [] { Account::get().signIn (false); };
    openAgain.onClick = [] { Account::get().openPageAgain(); };
    cancel.onClick = [] { Account::get().cancel(); };
    retry.onClick = [] { Account::get().refresh(); };
    monthly.onClick = [] { Account::get().upgrade ("month"); };
    yearly.onClick = [] { Account::get().upgrade ("year"); };
    compare.onClick = [] { Account::get().openUpgradePage(); };
    signOut.onClick = [] { Account::get().signOut(); };
    checkNow.onClick = [] { Account::get().checkPlan(); };
    stopWaiting.onClick = [] { Account::get().cancelUpgrade(); };
    for (auto* b : { &create, &signIn, &openAgain, &cancel, &retry, &monthly, &yearly, &compare, &signOut, &checkNow, &stopWaiting })
        addChildComponent (*b);

    Account::get().addChangeListener (this);
    refresh();
}

SignInView::~SignInView()
{
    Account::get().removeChangeListener (this);
}

void SignInView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void SignInView::refresh()
{
    const auto s = screen();
    const auto& a = Account::get();
    create.setVisible (s == Screen::signIn);
    signIn.setVisible (s == Screen::signIn);
    openAgain.setVisible (s == Screen::waiting);
    cancel.setVisible (s == Screen::starting || s == Screen::waiting);
    retry.setVisible (s == Screen::checking && a.error().isNotEmpty());

    const auto month = offerLabel ("month"), year = offerLabel ("year");
    const auto plans = s == Screen::plan && a.billingAvailable();
    monthly.setButtonText ("Monthly  " + juce::String::fromUTF8 ("\xc2\xb7") + "  " + month);
    yearly.setButtonText ("Yearly  " + juce::String::fromUTF8 ("\xc2\xb7") + "  " + year);
    monthly.setVisible (plans && month.isNotEmpty());
    yearly.setVisible (plans && year.isNotEmpty());
    compare.setVisible (s == Screen::plan);
    signOut.setVisible (s == Screen::plan || s == Screen::checking);
    retry.setVisible (retry.isVisible() || (s == Screen::plan && ! a.billingAvailable()));
    checkNow.setVisible (s == Screen::paying);
    stopWaiting.setVisible (s == Screen::paying);
    resized();
    repaint();
}

void SignInView::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::bg1);
    const auto& account = Account::get();
    auto area = content;
    const auto title = textFont (type::title + 4.0f, true), body = textFont (type::body);

    // Ghosty's logo
    ghosty::draw (g, area.removeFromTop (44).withSizeKeepingCentre (44, 44).toFloat(), colour::textPrimary);
    area.removeFromTop (14);

    auto error = [&] {
        if (account.error().isNotEmpty())
            line (g, account.error(), area, 36, textFont (type::small), colour::record);
    };

    switch (screen())
    {
        case Screen::signIn:
            line (g, "Welcome to Ghosty", area, 26, title, colour::textPrimary);
            line (g, "Sign in or create an account to start making music with Ghosty, LYNX's AI.", area, 40, body, colour::textSecondary);
            area.removeFromTop (54);
            error();
            break;

        case Screen::starting:
            line (g, "Opening your browser...", area, 26, title, colour::textPrimary);
            break;

        case Screen::waiting:
        {
            line (g, "Finish in your browser", area, 26, title, colour::textPrimary);
            line (g, "Sign in or create your account there, then approve this code:", area, 38, body, colour::textSecondary);
            area.removeFromTop (8);
            auto code = account.code();
            if (code.length() == 8)
                code = code.substring (0, 4) + "-" + code.substring (4); // (ABCD-EFGH)
            const auto box = area.removeFromTop (52).withSizeKeepingCentre (juce::jmin (area.getWidth(), 220), 52).toFloat();
            g.setColour (colour::bg2);
            g.fillRoundedRectangle (box, 8.0f);
            g.setColour (colour::bg4);
            g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
            g.setColour (colour::textPrimary);
            g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 24.0f, juce::Font::bold)));
            g.drawText (code, box, juce::Justification::centred);
            break;
        }

        case Screen::checking:
            line (g, "Checking your plan...", area, 26, title, colour::textPrimary);
            line (g, account.email(), area, 22, body, colour::textSecondary, 1);
            area.removeFromTop (54);
            error();
            break;

        case Screen::plan:
        {
            const auto trial = account.trialDays();
            line (g, trial > 0 ? "Start your " + juce::String (trial) + "-day free trial" : juce::String ("Upgrade to Launch"), area, 26, title,
                  colour::textPrimary);
            line (g,
                  trial > 0 ? "Ghosty is part of the Launch plan. Try it free for " + juce::String (trial) + " days, then pick monthly or yearly. "
                                  "Cancel anytime."
                            : juce::String ("Ghosty is part of the Launch plan. Pick monthly or yearly; cancel anytime."),
                  area, 58, body, colour::textSecondary, 3);
            area.removeFromTop (8 + 34 + 6 + 34 + 10); // (the plan buttons, then Compare plans / Sign out)
            if (! account.billingAvailable())
                line (g, "Plans aren't available right now. Try again in a moment.", area, 36, textFont (type::small), colour::warning);
            else if (trial > 0)
                line (g, "Nothing to pay today: your card is charged when the trial ends.", area, 22, textFont (type::small), colour::textDisabled);
            error();
            break;
        }

        case Screen::paying:
            line (g, "Finish the checkout in your browser", area, 26, title, colour::textPrimary);
            line (g, "Ghosty opens here by itself as soon as your plan starts.", area, 40, body, colour::textSecondary);
            area.removeFromTop (54);
            error();
            break;
    }

    // the footer: who's signed in, or why the browser
    auto footer = getLocalBounds().removeFromBottom (44).reduced (16, 0);
    g.setColour (colour::textDisabled);
    g.setFont (textFont (type::small));
    g.drawFittedText (account.state() == Account::State::signedIn ? "Signed in as " + account.email()
                                                                   : juce::String ("You sign in in your browser: Ghosty never sees your password."),
                      footer, juce::Justification::centred, 2);
}

void SignInView::resized()
{
    // a centred column, a little above the middle
    const auto width = juce::jmin (getWidth() - 32, 360);
    const auto height = 300;
    content = juce::Rectangle<int> ((getWidth() - width) / 2, juce::jmax (16, getHeight() / 2 - height / 2 - 30), width, height);

    // where each screen's text ends (the same heights paint uses: the logo 44 + 14, then the title and the lines)
    const auto s = screen();
    const auto text = s == Screen::signIn || s == Screen::paying ? 26 + 40 + 6
                    : s == Screen::starting                      ? 26 + 12
                    : s == Screen::waiting                       ? 26 + 38 + 8 + 52 + 16
                    : s == Screen::checking                      ? 26 + 22 + 10
                                                                 : 26 + 58 + 8; // (plan)
    const auto top = content.getY() + 44 + 14 + text;
    auto row = [&] (int y, std::initializer_list<std::pair<juce::Button*, int>> buttons) {
        int total = 0;
        for (const auto& [b, w] : buttons)
            total += w + (total > 0 ? 8 : 0);
        auto r = juce::Rectangle<int> (0, y, total, 34).withCentre ({ content.getCentreX(), y + 17 });
        for (const auto& [b, w] : buttons)
        {
            b->setBounds (r.removeFromLeft (w));
            r.removeFromLeft (8);
        }
    };

    switch (s)
    {
        case Screen::signIn: row (top, { { &create, 140 }, { &signIn, 100 } }); break;
        case Screen::starting: row (top, { { &cancel, 90 } }); break;
        case Screen::waiting: row (top, { { &openAgain, 160 }, { &cancel, 80 } }); break;
        case Screen::checking: row (top, { { &retry, 100 }, { &signOut, 90 } }); break;
        case Screen::paying: row (top, { { &checkNow, 170 }, { &stopWaiting, 110 } }); break;
        case Screen::plan:
        {
            const auto w = juce::jmin (176, (content.getWidth() - 8) / 2);
            if (monthly.isVisible() && yearly.isVisible())
                row (top, { { &monthly, w }, { &yearly, w } });
            else if (yearly.isVisible())
                row (top, { { &yearly, w } });
            else if (monthly.isVisible())
                row (top, { { &monthly, w } });
            else if (retry.isVisible())
                row (top, { { &retry, 100 } });
            row (top + 34 + 6, { { &compare, 120 }, { &signOut, 80 } });
            break;
        }
    }
}
} // namespace lynx::commands
