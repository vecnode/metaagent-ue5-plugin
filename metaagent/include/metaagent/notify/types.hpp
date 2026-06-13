#pragma once

#include "metaagent/core/types.hpp"
#include "metaagent/export.hpp"

namespace metaagent::notify {

struct NotifyMessage {
	core::String text;
	core::String raw_body;
	bool parsed_from_json = false;
};

struct NotifyParseResult {
	bool success = false;
	NotifyMessage message;
};

} // namespace metaagent::notify
