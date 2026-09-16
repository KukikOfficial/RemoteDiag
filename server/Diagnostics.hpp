// server/Diagnostics.hpp
#ifndef DIAGNOSTICS_HPP
#define DIAGNOSTICS_HPP
#include "../common/Protocol.hpp"

class Diagnostics {
public:
    static DiagnosticsPayload CollectMetrics();
};
#endif