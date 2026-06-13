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

#include "metaagent/initialize.cpp"
