cmake_minimum_required(VERSION 3.16)
project(metaagent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(metaagent STATIC
    src/metaagent/initialize.cpp
    src/metaagent/core/types.cpp
    src/metaagent/core/math.cpp
    src/metaagent/core/log_sink.cpp
    src/metaagent/particle/forming_types.cpp
    src/metaagent/particle/return_types.cpp
    src/metaagent/particle/pattern_types.cpp
    src/metaagent/particle/representation_types.cpp
    src/metaagent/particle/transition_graph.cpp
    src/metaagent/particle/forming_solver.cpp
    src/metaagent/particle/actuation_math.cpp
    src/metaagent/particle/shape_types.cpp
    src/metaagent/particle/shape_builder.cpp
    src/metaagent/particle/image_mask_processor.cpp
    src/metaagent/particle/scheduler.cpp
)

target_include_directories(metaagent
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(metaagent
    PUBLIC
        METAAGENT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        METAAGENT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        METAAGENT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(MSVC)
    target_compile_options(metaagent PRIVATE /W4)
else()
    target_compile_options(metaagent PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(metaagent_transition_graph_test tests/transition_graph_test.cpp)
target_link_libraries(metaagent_transition_graph_test PRIVATE metaagent)
add_test(NAME transition_graph_test COMMAND metaagent_transition_graph_test)

add_executable(metaagent_forming_types_test tests/forming_types_test.cpp)
target_link_libraries(metaagent_forming_types_test PRIVATE metaagent)
add_test(NAME forming_types_test COMMAND metaagent_forming_types_test)

install(TARGETS metaagent
    EXPORT metaagentTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/metaagent DESTINATION include)

install(EXPORT metaagentTargets
    FILE metaagentTargets.cmake
    NAMESPACE metaagent::
    DESTINATION lib/cmake/metaagent
)
