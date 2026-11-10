#pragma once

// account: the LYNX account this app is signed in to (console/Account.h: a device sign-in through the browser).
//
//   account.status      signed in or not, who (name, email, plan), the server; while signing in, the code to approve
//   account.signin      starts a sign-in: the browser opens on the approval page (account status shows the code)
//   account.signout     signs out (the session ends on the server; the token leaves the Keychain)
//   account.upgrade [--billing monthly|yearly]
//                       the Launch plan: Dodo's checkout opens in the browser; LYNX switches once the payment is through
//   account.manage      the subscription in Dodo's customer portal (cancel, card, invoices)

#include "CommandRegistry/core/CommandModule.h"

namespace lynx::commands
{
class AccountModule final : public CommandModule
{
public:
    AccountModule() : CommandModule ("account", "Ghosty (LYNX's AI): the account this app is signed in to, and its plan") {}
    void define() override;
};
} // namespace lynx::commands
