# The MimicPhon audio engine. Plain C++17, no JUCE, no plugin code.
#   include/mph/core/   generic, reusable parts (the tempo clock from Logic, level helpers)
#   include/mph/        the engine (Engine.h) and the list of control values it understands (Params.h)

add_library(mph_engine STATIC
    src/Engine.cpp
)

target_include_directories(mph_engine PUBLIC include)
target_compile_features(mph_engine PUBLIC cxx_std_17)

if(MPH_BUILD_TESTS)
    add_executable(mph_engine_test tests/engine_test.cpp)
    target_link_libraries(mph_engine_test PRIVATE mph_engine)
    add_test(NAME engine COMMAND mph_engine_test)
endif()
