#include "metaagent.h"

#include <cassert>

int main()
{
	using namespace metaagent::app;
	using namespace metaagent::core;

	const GuiPanelCatalog catalog = build_gui_panel_catalog();
	assert(catalog.sections.size() >= 6);

	bool found_networking = false;
	bool found_ai = false;
	bool found_recording = false;
	for (const GuiPanelSection& section : catalog.sections)
	{
		if (section.section_id == "Networking")
		{
			found_networking = true;
			assert(section.rows.size() == 2);
			assert(section.rows[0].action_id == "StartAudio");
			assert(section.rows[1].action_id == "StartImage");
		}
		if (section.section_id == "Camera")
		{
			assert(section.rows.size() == 3);
			assert(section.rows[2].action_id == "CycleCinematicStyle");
		}
		if (section.section_id == "AI")
		{
			found_ai = true;
			assert(section.rows.size() == 1);
			assert(section.rows[0].action_id == "ToggleAutopilot");
		}
		if (section.section_id == "Recording")
		{
			found_recording = true;
			assert(section.rows.size() == 2);
			assert(section.rows[0].action_id == "ToggleRecording");
		}
	}

	assert(found_networking);
	assert(found_ai);
	assert(found_recording);

	String feature;
	assert(section_runtime_feature("Particle", feature));
	assert(feature == "particle");

	return 0;
}
