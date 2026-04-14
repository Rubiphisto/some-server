function(some_add_source FILENAME)
    get_filename_component(_FILENAME_EXT ${FILENAME} EXT)
    get_filename_component( _FILENAME_EXT ${FILENAME} EXT )
    if( _FILENAME_EXT STREQUAL ".h" OR _FILENAME_EXT STREQUAL ".hpp" )
        set( HEADER_FILES ${HEADER_FILES} ${FILENAME} PARENT_SCOPE )
    elseif( _FILENAME_EXT STREQUAL ".cpp" )
        set( SOURCE_FILES ${SOURCE_FILES} ${FILENAME} PARENT_SCOPE )
    endif()
endfunction()

function(some_configure_common TARGETNAME)
    set_target_properties(${TARGETNAME} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY ${SERVER_BINARY_FOLDER}/lib
            LIBRARY_OUTPUT_DIRECTORY ${SERVER_BINARY_FOLDER}/lib
            RUNTIME_OUTPUT_DIRECTORY ${SERVER_BINARY_FOLDER}/bin
    )
endfunction()

function(some_add_library DEF_NAME)
    message(STATUS "add library: ${DEF_NAME}")
endfunction()

function(some_add_definition DEF_NAME)
    message(STATUS "add definition: ${DEF_NAME}")
endfunction()

function(some_library LIBRARYNAME)
    message(STATUS "====== Configuring library: ${LIBRARYNAME} ======")
    set(HEADER_FILES "")
    set(SOURCE_FILES "")
    set(TARGET_LIBS "")
    set(TARGET_DEFS "")
    set(SECTION_NAME "")
    include_directories(
            .
            ${CMAKE_SOURCE_DIR}/src
    )
    add_definitions(-Wall -Wno-long-long -pedantic -g -finput-charset=UTF-8 ${CMAKE_CXX_FLAGS})

    add_library(${LIBRARYNAME} STATIC "")
    some_configure_common(${LIBRARYNAME})

    # parameters
    set(SECTIONS_GROUP "^LIBS|DEFS$")
    if (${ARGC} GREATER 1)
        math(EXPR list_max_index "${ARGC}-1")
        foreach (index RANGE 1 ${list_max_index})
            list (GET ARGV ${index} value)
            if ("${value}" MATCHES ${SECTIONS_GROUP})
                string(REGEX MATCH "${SECTIONS_GROUP}" SECTION_NAME "${value}")
            elseif ("${SECTION_NAME}" STREQUAL "LIBS")
                list(APPEND TARGET_LIBS "${value}")
            elseif ("${SECTION_NAME}" STREQUAL "DEFS")
                list(APPEND TARGET_DEFS "${value}")
            endif ()
        endforeach (index)
    endif ()
    include(${LIBRARYNAME}.cmake)
    target_sources(${LIBRARYNAME} PRIVATE ${HEADER_FILES} ${SOURCE_FILES})
    if (TARGET_LIBS)
        target_link_libraries(${LIBRARYNAME} PUBLIC ${TARGET_LIBS})
    endif ()
    if (TARGET_DEFS)
        target_compile_definitions(${LIBRARYNAME} PUBLIC ${TARGET_DEFS})
    endif ()
endfunction()

function(some_application APPNAME)
    message(STATUS "====== Configuring application: ${APPNAME} ======")
    set(HEADER_FILES "")
    set(SOURCE_FILES "")
    set(TARGET_LIBS "")
    set(TARGET_DEFS "")
    set(SECTION_NAME "")
    include_directories(
            .
            ${CMAKE_SOURCE_DIR}/src
    )
    add_definitions(-Wall -Wno-long-long -pedantic -g -finput-charset=UTF-8 ${CMAKE_CXX_FLAGS})

    add_executable(${APPNAME})
    some_configure_common(${APPNAME})

    # parameters
    set(SECTIONS_GROUP "^LIBS|DEFS$")
    if (${ARGC} GREATER 1)
        math(EXPR list_max_index "${ARGC}-1")
        foreach (index RANGE 1 ${list_max_index})
            list (GET ARGV ${index} value)
            if ("${value}" MATCHES ${SECTIONS_GROUP})
                string(REGEX MATCH "${SECTIONS_GROUP}" SECTION_NAME "${value}")
            elseif ("${SECTION_NAME}" STREQUAL "LIBS")
                list(APPEND TARGET_LIBS "${value}")
            elseif ("${SECTION_NAME}" STREQUAL "DEFS")
                list(APPEND TARGET_DEFS "${value}")
            endif ()
        endforeach (index)
    endif ()
    include(${APPNAME}.cmake)
    target_sources(${APPNAME} PRIVATE ${HEADER_FILES} ${SOURCE_FILES})
    if (TARGET_LIBS)
        target_link_libraries(${APPNAME} PRIVATE ${TARGET_LIBS})
    endif ()
    if (TARGET_DEFS)
        target_compile_definitions(${APPNAME} PRIVATE ${TARGET_DEFS})
    endif ()
endfunction()
