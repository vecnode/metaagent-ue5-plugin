cmake_minimum_required(VERSION 3.16)
project(metaagent VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(metaagent STATIC
    metaagent.cpp
)

target_include_directories(metaagent
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
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

add_executable(metaagent_shape_builder_polyline_test tests/shape_builder_polyline_test.cpp)
target_link_libraries(metaagent_shape_builder_polyline_test PRIVATE metaagent)
add_test(NAME shape_builder_polyline_test COMMAND metaagent_shape_builder_polyline_test)

add_executable(metaagent_actuation_composer_test tests/actuation_composer_test.cpp)
target_link_libraries(metaagent_actuation_composer_test PRIVATE metaagent)
add_test(NAME actuation_composer_test COMMAND metaagent_actuation_composer_test)

add_executable(metaagent_media_decode_test tests/media_decode_test.cpp)
target_link_libraries(metaagent_media_decode_test PRIVATE metaagent)
add_test(NAME media_decode_test COMMAND metaagent_media_decode_test)

add_executable(metaagent_camera_rig_test tests/camera_rig_test.cpp)
target_link_libraries(metaagent_camera_rig_test PRIVATE metaagent)
add_test(NAME camera_rig_test COMMAND metaagent_camera_rig_test)

install(TARGETS metaagent
    EXPORT metaagentTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES metaagent.h DESTINATION include)
install(DIRECTORY include/metaagent DESTINATION include)

install(EXPORT metaagentTargets
    FILE metaagentTargets.cmake
    NAMESPACE metaagent::
    DESTINATION lib/cmake/metaagent
)
