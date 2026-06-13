#pragma once

#include "metaagent/export.hpp"
#include "metaagent/session/types.hpp"

namespace metaagent::session {

METAAGENT_API core::String build_http_server_status_text(const RuntimeSession& session);

METAAGENT_API core::String build_startup_feature_flags_text(const RuntimeSession& session);

} // namespace metaagent::session
