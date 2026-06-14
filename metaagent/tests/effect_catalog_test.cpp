#include "metaagent.h"

#include <cassert>

int main()
{
	using namespace metaagent::particle;
	using namespace metaagent::core;

	const Array<ParticleGuiActionSpec> specs = particle_gui_action_specs();
	assert(specs.size() == 8);

	const ParticleGuiActionSpec* load = find_particle_gui_action("ParticleLoadPreview");
	assert(load && load->dispatch_kind == ParticleGuiDispatchKind::LoadPreviewPng);

	const ParticleGuiActionSpec* step = find_particle_gui_action("ParticleStepForward");
	assert(step && step->effect_id == "PatternStepForward");

	const ParticleGuiActionSpec* preset = find_particle_gui_action("ParticleCyclePreset");
	assert(preset && preset->effect_id == "CyclePreset");

	const Array<metaagent::app::GuiPanelRow> rows = particle_gui_panel_rows();
	assert(rows.size() == specs.size());

	return 0;
}
