#include "metaagent/initialize.hpp"

#include "metaagent/particle/forming_solver.hpp"
#include "metaagent/particle/transition_graph.hpp"

namespace metaagent {

void initialize_defaults()
{
	particle::TransitionGraph::register_defaults();
	particle::FormingSolverRegistry::register_defaults();
}

} // namespace metaagent
