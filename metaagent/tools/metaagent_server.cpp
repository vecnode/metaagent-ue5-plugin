#include "metaagent.h"
#include "tools/mini_http_server.hpp"

#include <csignal>
#include <iostream>
#include <string>

namespace {

volatile bool g_running = true;

void handle_signal(int)
{
	g_running = false;
}

int parse_port(int argc, char** argv, int default_port)
{
	for (int index = 1; index < argc; ++index)
	{
		const std::string arg = argv[index];
		if (arg == "--port" && index + 1 < argc)
		{
			return std::max(1, std::atoi(argv[index + 1]));
		}
	}
	return default_port;
}

} // namespace

int main(int argc, char** argv)
{
	metaagent::initialize_defaults();

	const int port = parse_port(argc, argv, 30080);
	metaagent::tools::MiniHttpServer server;
	metaagent::tools::MiniHttpServerOptions options;
	options.port = port;
	options.session.active = true;
	options.session.map_name = "metaagent_server";
	options.session.build_label = "standalone";
	options.session.http_enabled = true;
	options.session.http_router_bound = true;
	options.on_notify = [](const metaagent::core::String& message)
	{
		std::cout << "[notify] " << message << std::endl;
	};

	if (!server.start(options))
	{
		std::cerr << "Failed to bind HTTP server on port " << port << std::endl;
		return 1;
	}

	std::signal(SIGINT, handle_signal);
#if !defined(_WIN32)
	std::signal(SIGTERM, handle_signal);
#endif

	std::cout << "metaagent_server listening on http://127.0.0.1:" << port
		<< " (/health /echo /notify)" << std::endl;

	while (g_running)
	{
		if (!server.poll_once(200))
		{
			break;
		}
	}

	server.stop();
	std::cout << "metaagent_server stopped." << std::endl;
	return 0;
}
