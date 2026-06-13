#include "metaagent.h"

#include <cassert>

int main()
{
	using namespace metaagent::net;

	PlatformEndpointConfig config;
	config.enabled = true;
	config.base_url = "http://127.0.0.1:8000";
	config.event_endpoint = "/api/unreal/event";
	config.session_id = "session-1";

	assert(build_platform_url(config) == "http://127.0.0.1:8000/api/unreal/event");

	PlatformEvent event;
	event.source = "keyboard";
	event.event_name = "key_pressed";
	event.message = "start audio";
	event.timestamp_utc = "2026-06-13T12:00:00Z";
	event.metadata.map_name = "test_map";
	event.metadata.build_label = "Development";

	const PlatformOutboundRequest request = build_platform_outbound_request(config, event);
	assert(request.valid);
	assert(request.url == "http://127.0.0.1:8000/api/unreal/event");
	assert(request.body.find("\"event\":\"key_pressed\"") != std::string::npos);
	assert(request.body.find("start audio") != std::string::npos);
	assert(request.body.find("test_map") != std::string::npos);

	const PlatformEventResponse ok = parse_platform_event_response(
		200,
		"{\"agent_running\":true,\"agent_action\":\"listen\"}",
		true);
	assert(ok.http_success);
	assert(ok.agent_running);
	assert(ok.agent_action == "listen");
	assert(ok.user_message.find("LISTEN") != std::string::npos);

	config.enabled = false;
	const PlatformOutboundRequest disabled = build_platform_outbound_request(config, event);
	assert(!disabled.valid);

	return 0;
}
