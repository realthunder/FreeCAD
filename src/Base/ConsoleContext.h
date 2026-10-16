// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef BASE_CONSOLECONTEXT_H
#define BASE_CONSOLECONTEXT_H

#include <string>

#include <FCGlobal.h>

namespace Base
{

/** While one lives, the warnings and errors this thread sends to the console
 * name \a name where their text does not already, as " [name]" at the end.
 *
 * Set around an object's recompute: what its execute() and every helper it
 * calls report -- swallowed exceptions included -- says whose it is
 * (docs/HandsOnQueue.md entry 164). Nested scopes name the innermost; a
 * message queued to the main thread is named where it was sent.
 */
class BaseExport ConsoleContext
{
public:
    explicit ConsoleContext(const std::string& name);
    ~ConsoleContext();
    ConsoleContext(const ConsoleContext&) = delete;
    ConsoleContext& operator=(const ConsoleContext&) = delete;

private:
    std::string previous;
};

}  // namespace Base

#endif  // BASE_CONSOLECONTEXT_H
