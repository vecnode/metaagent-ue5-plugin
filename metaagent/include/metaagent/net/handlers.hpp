#pragma once

#include "metaagent/export.hpp"
#include "metaagent/net/types.hpp"
#include "metaagent/notify/types.hpp"
#include "metaagent/session/types.hpp"

namespace metaagent::net {

struct HandlerContext {
	session::RuntimeSession session;
};

struct NotifyHandleResult {
	HttpResponse response;
	notify::NotifyMessage notify_message;
	bool has_notify_message = false;
};

METAAGENT_API HttpResponse handle_health(const HandlerContext& context);

METAAGENT_API HttpResponse handle_echo(const HttpRequest& request);

METAAGENT_API NotifyHandleResult handle_notify(const HttpRequest& request);

} // namespace metaagent::net
