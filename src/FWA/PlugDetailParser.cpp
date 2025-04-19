#include "FWA/PlugDetailParser.hpp"

namespace FWA {
// Implementation will be added during refactor
PlugDetailParser::PlugDetailParser(CommandInterface* commandInterface)
    : commandInterface_(commandInterface), streamFormatOpcode_(0xBF) {}
} // namespace FWA
