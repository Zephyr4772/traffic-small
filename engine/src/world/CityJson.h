#pragma once
#include <string>

#include "world/City.h"

namespace sim {

// Compact JSON for the browser. Arrays instead of objects keep a medium city around 0.5 MB.
// The field order of every array is documented in web/js/city.js (decodeCity).
std::string cityToJson(const City& city);

}  // namespace sim
