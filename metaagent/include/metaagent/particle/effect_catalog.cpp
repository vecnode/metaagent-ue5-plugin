#include "metaagent/particle/effect_catalog.hpp"

namespace metaagent::particle {
namespace {

ParticleGuiActionSpec make_spec(
	const char* action_id,
	const char* key,
	const char* description,
	const ParticleGuiDispatchKind kind,
	const char* effect_id = "")
{
	ParticleGuiActionSpec spec;
	spec.gui_action_id = action_id;
	spec.key_label = key;
	spec.description = description;
	spec.dispatch_kind = kind;
	spec.effect_id = effect_id;
	return spec;
}

core::Array<ParticleGuiActionSpec> build_specs()
{
	core::Array<ParticleGuiActionSpec> specs;
	specs.push_back(make_spec(
		"ParticleLoadPreview",
		"F",
		"Load sdxl_latest.png preview + image shape source",
		ParticleGuiDispatchKind::LoadPreviewPng));
	specs.push_back(make_spec(
		"ParticleStepBackward",
		",",
		"Step pattern state backward",
		ParticleGuiDispatchKind::TriggerEffect,
		"PatternStepBackward"));
	specs.push_back(make_spec(
		"ParticleStepForward",
		".",
		"Step pattern state forward (Idle starts Forming)",
		ParticleGuiDispatchKind::TriggerEffect,
		"PatternStepForward"));
	specs.push_back(make_spec(
		"ParticleSlowPreset",
		"B",
		"Apply Slow preset",
		ParticleGuiDispatchKind::TriggerEffect,
		"PresetSlow"));
	specs.push_back(make_spec(
		"ParticleDramaticPreset",
		"N",
		"Apply Dramatic preset",
		ParticleGuiDispatchKind::TriggerEffect,
		"PresetDramatic"));
	return specs;
}

} // namespace

core::Array<ParticleGuiActionSpec> particle_gui_action_specs()
{
	return build_specs();
}

const ParticleGuiActionSpec* find_particle_gui_action(const core::String& gui_action_id)
{
	for (const ParticleGuiActionSpec& spec : build_specs())
	{
		if (spec.gui_action_id == gui_action_id)
		{
			return &spec;
		}
	}
	return nullptr;
}

core::Array<app::GuiPanelRow> particle_gui_panel_rows()
{
	core::Array<app::GuiPanelRow> rows;
	for (const ParticleGuiActionSpec& spec : build_specs())
	{
		app::GuiPanelRow row;
		row.action_id = spec.gui_action_id;
		row.key_label = spec.key_label;
		row.description = spec.description;
		rows.push_back(row);
	}
	return rows;
}

} // namespace metaagent::particle
