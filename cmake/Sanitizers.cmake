add_library(breakwater_sanitizers INTERFACE)

if(BREAKWATER_SANITIZE)
    set(_san_flags -fsanitize=${BREAKWATER_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_compile_options(breakwater_sanitizers INTERFACE ${_san_flags})
    target_link_options(breakwater_sanitizers INTERFACE ${_san_flags})
endif()
