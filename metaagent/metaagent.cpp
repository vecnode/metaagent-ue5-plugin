/**
 * MetaAgent amalgamated implementation.
 * All library .cpp units live under include/metaagent/ and are compiled here.
 */

#include "metaagent/core/types.cpp"
#include "metaagent/core/math.cpp"
#include "metaagent/core/log_sink.cpp"

#include "metaagent/media/decode.cpp"
#include "metaagent/media/store.cpp"
#include "metaagent/media/pipeline.cpp"
#include "metaagent/media/mask_cache.cpp"

#include "metaagent/camera/types.cpp"
#include "metaagent/camera/rig.cpp"
#include "metaagent/camera/controller.cpp"

#include "metaagent/particle/forming_types.cpp"
#include "metaagent/particle/return_types.cpp"
#include "metaagent/particle/pattern_types.cpp"
#include "metaagent/particle/representation_types.cpp"
#include "metaagent/particle/transition_graph.cpp"
#include "metaagent/particle/forming_solver.cpp"
#include "metaagent/particle/actuation_math.cpp"
#include "metaagent/particle/representation_actuation.cpp"
#include "metaagent/particle/shape_types.cpp"
#include "metaagent/particle/shape_builder.cpp"
#include "metaagent/particle/image_mask_processor.cpp"
#include "metaagent/particle/scheduler.cpp"
#include "metaagent/particle/visual_continuity.cpp"

#include "metaagent/app/commands.cpp"
#include "metaagent/app/gui_actions.cpp"
#include "metaagent/app/gui_catalog.cpp"
#include "metaagent/input/policy.cpp"
#include "metaagent/particle/effect_catalog.cpp"
#include "metaagent/particle/state_effects.cpp"
#include "metaagent/runtime/host_interfaces.cpp"
#include "metaagent/net/json.cpp"
#include "metaagent/net/handlers.cpp"
#include "metaagent/net/router.cpp"
#include "metaagent/net/platform_client.cpp"
#include "metaagent/notify/parse.cpp"
#include "metaagent/session/status.cpp"

#include "metaagent/initialize.cpp"
