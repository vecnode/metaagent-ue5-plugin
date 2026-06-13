#pragma once

#include "metaagent/notify/types.hpp"
#include "metaagent/export.hpp"

namespace metaagent::notify {

METAAGENT_API NotifyParseResult parse_notify_body(const core::String& body);

} // namespace metaagent::notify
