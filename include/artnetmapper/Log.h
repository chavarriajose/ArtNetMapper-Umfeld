// ArtNetMapper for Umfeld :: console output. MIT License.
//
// The core prints through these two functions. In a sketch they go to Umfeld's
// console() and warning(); in the unit tests to stdout.

#pragma once

#include <functional>
#include <string>

namespace umfeld::artnetmapper {

    struct Log {
        /** Prints "ArtNetMapper: <message>". */
        static void info(const std::string& message) { infoOut()("ArtNetMapper: " + message); }
        /** Prints "ArtNetMapper: warning: <message>". */
        static void warn(const std::string& message) { warnOut()("ArtNetMapper: warning: " + message); }

        static std::function<void(const std::string&)>& infoOut();
        static std::function<void(const std::string&)>& warnOut();
    };

} // namespace umfeld::artnetmapper
