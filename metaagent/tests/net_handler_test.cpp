#include "metaagent.h"

#include <cassert>
#include <cstring>

int main()
{
	using namespace metaagent::net;

	HandlerContext context;
	context.session.map_name = "test_map";
	context.session.build_label = "Development";

	const HttpResponse health = handle_health(context);
	assert(health.body.find("\"status\":\"ok\"") != std::string::npos);
	assert(health.body.find("test_map") != std::string::npos);

	HttpRequest echo_request;
	echo_request.method = HttpMethod::Get;
	echo_request.path = "/echo";
	echo_request.query_string = "msg=hello";
	const HttpResponse echo = handle_echo(echo_request);
	assert(echo.body.find("hello") != std::string::npos);

	HttpRequest notify_request;
	notify_request.method = HttpMethod::Post;
	notify_request.path = "/notify";
	notify_request.body = "{\"message\":\"particle_ready\"}";
	const NotifyHandleResult notify = handle_notify(notify_request);
	assert(notify.response.body.find("true") != std::string::npos);
	assert(notify.notify_message.text == "particle_ready");

	RouteTable routes;
	HttpRequest routed_notify = notify_request;
	RouteDispatchResult dispatch = routes.dispatch(routed_notify, context);
	assert(dispatch.handled);
	assert(dispatch.notify.notify_message.text == "particle_ready");

	const metaagent::notify::NotifyParseResult parsed =
		metaagent::notify::parse_notify_body("{\"message\":\"from_core\"}");
	assert(parsed.message.text == "from_core");

	const metaagent::app::CommandResult validation = metaagent::app::validate_command(
		metaagent::app::CommandId::ToggleNetworkingRuntime,
		context.session);
	assert(validation.success);

	return 0;
}
