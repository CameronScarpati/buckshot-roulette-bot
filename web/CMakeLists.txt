# The engine for the web page: the engine and the solver with web/bridge.cpp
# on top, compiled to WebAssembly. Only an Emscripten build reaches this file
# (emcmake cmake), so a native build, its tests and CI never see it.
#
#   emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
#   cmake --build build-web --target web_engine
#
# The result is build-web/web/engine.mjs and engine.wasm, an ES module that
# web/js/engine-worker.js imports.

add_executable(web_engine bridge.cpp)
target_link_libraries(web_engine PRIVATE bsr_core bsr_warnings)
set_target_properties(web_engine PROPERTIES OUTPUT_NAME engine SUFFIX ".mjs")

target_link_options(web_engine PRIVATE
    -lembind
    # One factory, imported as an ES module by a module Worker, or by Node in
    # the smoke test (web/test/smoke.mjs).
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createEngine
    -sENVIRONMENT=worker,node
    # No eval or new Function, so the page needs no unsafe-eval in a content
    # security policy.
    -sDYNAMIC_EXECUTION=0
    # The engine reads and writes no files.
    -sFILESYSTEM=0
    # The solver's tables grow with the positions a search meets, up to the
    # node limits in bridge.cpp. On the heaviest positions tried (eight shells
    # and eight items a seat) the heap reached 298 MB for a ranking in a round
    # and 207 MB for a written position, so 512 MB leaves room while asking
    # little of a phone. A search that passed it would stop with an error that
    # the worker reports. The default 64 KB stack is kept: the same searches
    # ran within 16 KB.
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=512MB
)
