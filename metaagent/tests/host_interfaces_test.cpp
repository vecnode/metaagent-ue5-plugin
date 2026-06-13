#include "metaagent.h"

#include <cassert>

int main()
{
	using namespace metaagent::runtime;

	assert(!default_recording_snapshot().capture_active);
	assert(!default_ai_snapshot().autopilot_enabled);
	assert(!invoke_toggle_recording({}));
	assert(!invoke_toggle_autopilot({}));

	bool toggled = false;
	HostServiceCallbacks callbacks;
	callbacks.toggle_recording = [&toggled]() { toggled = true; return true; };
	assert(invoke_toggle_recording(callbacks));
	assert(toggled);

	return 0;
}
