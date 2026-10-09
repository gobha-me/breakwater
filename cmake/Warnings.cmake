add_library(breakwater_warnings INTERFACE)

target_compile_options(
    breakwater_warnings
    INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Wimplicit-fallthrough
        $<$<BOOL:${BREAKWATER_WERROR}>:-Werror>
)
