#pragma once

/**
 * MetaAgent portable library — public entry point.
 *
 * Implementation is compiled from a single translation unit:
 *   metaagent/metaagent.cpp
 *
 * Embed in other engines by adding `metaagent/include` to your include path and
 * compiling `metaagent.cpp` once (see MetaAgentCoreAggregate.cpp in the UE plugin).
 */

#include "metaagent/export.hpp"
#include "metaagent/version.hpp"
#include "metaagent/initialize.hpp"

#include "metaagent/core/log_sink.hpp"
#include "metaagent/core/math.hpp"
#include "metaagent/core/types.hpp"

#include "metaagent/media/decode.hpp"
#include "metaagent/media/image.hpp"
#include "metaagent/media/mask_cache.hpp"
#include "metaagent/media/pipeline.hpp"
#include "metaagent/media/store.hpp"

#include "metaagent/camera/controller.hpp"
#include "metaagent/camera/rig.hpp"
#include "metaagent/camera/types.hpp"

#include "metaagent/particle/actuation_math.hpp"
#include "metaagent/particle/forming_solver.hpp"
#include "metaagent/particle/forming_types.hpp"
#include "metaagent/particle/image_mask_processor.hpp"
#include "metaagent/particle/pattern_types.hpp"
#include "metaagent/particle/representation_actuation.hpp"
#include "metaagent/particle/representation_types.hpp"
#include "metaagent/particle/return_types.hpp"
#include "metaagent/particle/scheduler.hpp"
#include "metaagent/particle/shape_builder.hpp"
#include "metaagent/particle/shape_types.hpp"
#include "metaagent/particle/transition_graph.hpp"
#include "metaagent/particle/visual_continuity.hpp"
#include "metaagent/particle/effect_catalog.hpp"
#include "metaagent/particle/state_effects.hpp"

#include "metaagent/runtime/host_interfaces.hpp"
#include "metaagent/app/gui_actions.hpp"
#include "metaagent/app/gui_catalog.hpp"
#include "metaagent/input/policy.hpp"
#include "metaagent/net/handlers.hpp"
#include "metaagent/net/json.hpp"
#include "metaagent/net/router.hpp"
#include "metaagent/net/platform_client.hpp"
#include "metaagent/net/types.hpp"
#include "metaagent/notify/parse.hpp"
#include "metaagent/session/status.hpp"
#include "metaagent/session/types.hpp"
