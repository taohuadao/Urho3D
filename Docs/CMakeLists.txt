# Copyright (c) 2008-2022 the Urho3D project
# License: MIT

# Set project name
project (Urho3D-Docs)

if (URHO3D_DOCS_QUIET)
    set (URHO3D_DOCS 1)
endif ()

# Macro for enabling IDE-specific integration with Urho3D documentation (See comments in Doxyfile for more detail instructions)
macro (enable_help IDE)
    if (${IDE} OR CMAKE_EXTRA_GENERATOR MATCHES ${IDE})
        set (${IDE}_HELP YES)
    else ()
        set (${IDE}_HELP NO)
    endif ()
endmacro ()

# There could be bug in CMake find_package() command, it currently does not honor NO_CMAKE_FIND_ROOT_PATH option for a non-rooted search as per CMake's documentation
# As a workaround, we unset CMAKE_FIND_ROOT_PATH (even when we are cross-compiling) but in this scope ONLY in order to always do a non-rooted search for Doxygen package
unset (CMAKE_FIND_ROOT_PATH)
# Find Doxygen and DOT packages
if (URHO3D_DOCS)
    set (REQUIRED REQUIRED)
endif ()
# https://github.com/urho3d/Urho3D/issues/2757
find_package (Doxygen 1.9.2 ${REQUIRED})

if (DOXYGEN_FOUND)
    # It is not an error when DOT is not found, instead just switching off the Doxygen's HAVE_DOT option
    find_package_handle_standard_args (Dot REQUIRED_VARS DOXYGEN_DOT_EXECUTABLE)

    # Generate platform specific Doxyfile automatically
    if (NOT URHO3D_OPENGL EQUAL DOXYFILE_URHO3D_OPENGL OR ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in IS_NEWER_THAN ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
        set (DOXYFILE_URHO3D_OPENGL ${URHO3D_OPENGL} CACHE INTERNAL "URHO3D_OPENGL build option when Doxyfile was last generated")
        if (URHO3D_OPENGL)
            set (EXCLUDE_GRAPHICS_API Direct3D9)
            set (EXCLUDE_GRAPHICS_API2 Direct3D11)
        else ()
            set (EXCLUDE_GRAPHICS_API OpenGL)
            if (URHO3D_D3D11)
                set (EXCLUDE_GRAPHICS_API2 Direct3D9)
            else ()
                set (EXCLUDE_GRAPHICS_API2 Direct3D11)
            endif ()
        endif ()
        foreach (IDE XCODE MSVC Eclipse CodeBlocks)
            enable_help (${IDE})
        endforeach ()
        if (MSVC_HELP)
            find_package (HTMLHelp)
            # It is not an error when HTML help compiler is not found, instead just switching off the Doxygen's HHC option
            find_package_handle_standard_args (HTMLHelp REQUIRED_VARS HTML_HELP_COMPILER)
        endif ()
        configure_file (${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Doxyfile)
    endif ()

    # Dump AngelScript and LuaScript API to Doxygen file if the corresponding host tool is available
    if (TARGET ScriptCompiler AND NOT CMAKE_CROSSCOMPILING)
        add_custom_command (OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/ScriptAPI.dox ${CMAKE_CURRENT_BINARY_DIR}/generated/AngelScriptAPI.h
            COMMAND ${CMAKE_BINARY_DIR}/bin/tool/ScriptCompiler -dumpapi ${CMAKE_SOURCE_DIR} ScriptAPI.dox AngelScriptAPI.h
            DEPENDS ScriptCompiler     # ScriptCompiler implicitly depends on Urho3D already
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated
            COMMENT "Dumping AngelScript API to ScriptAPI.dox")
    else ()
        add_custom_command (OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/ScriptAPI.dox ${CMAKE_CURRENT_BINARY_DIR}/generated/AngelScriptAPI.h
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/ScriptAPI.dox ${CMAKE_CURRENT_BINARY_DIR}/generated/ScriptAPI.dox
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/AngelScriptAPI.h ${CMAKE_CURRENT_BINARY_DIR}/generated/AngelScriptAPI.h
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ScriptAPI.dox ${CMAKE_CURRENT_SOURCE_DIR}/AngelScriptAPI.h)
    endif ()
    if (TARGET tolua++ AND NOT CMAKE_CROSSCOMPILING)
        file (GLOB PKGS RELATIVE ${CMAKE_SOURCE_DIR}/Source/Urho3D/LuaScript/pkgs ${CMAKE_SOURCE_DIR}/Source/Urho3D/LuaScript/pkgs/*.pkg)
        list (SORT PKGS)
        set (PKGLIST "// This is a generated file. DO NOT EDIT!\n\n")
        foreach (PKG ${PKGS})
            set (PKGLIST "${PKGLIST}$pfile \"${PKG}\"\n")
        endforeach ()
        file (WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaPkgToDox.txt.new ${PKGLIST})
        execute_process (COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaPkgToDox.txt.new ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaPkgToDox.txt)
        execute_process (COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaPkgToDox.txt.new)
        file (GLOB_RECURSE API_PKG_FILES ${CMAKE_SOURCE_DIR}/Source/Urho3D/LuaScript/pkgs/*.pkg)
        add_custom_command (OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaScriptAPI.dox
            COMMAND ${CMAKE_BINARY_DIR}/bin/tool/tolua++ -L ToDoxHook.lua -P -o ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaScriptAPI.dox ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaPkgToDox.txt
            DEPENDS tolua++ ${API_PKG_FILES} ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaPkgToDox.txt ${CMAKE_SOURCE_DIR}/Source/Urho3D/LuaScript/pkgs/ToDoxHook.lua
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/Source/Urho3D/LuaScript/pkgs
            COMMENT "Dumping LuaScript API to LuaScriptAPI.dox")
    else ()
        add_custom_command (OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaScriptAPI.dox
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/LuaScriptAPI.dox ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaScriptAPI.dox
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/LuaScriptAPI.dox)
    endif ()

    # In quiet mode, redirect standard output stream of Doxygen to a null device
    if (URHO3D_DOCS_QUIET)
        set (REDIRECT_STDOUT 1>${NULL_DEVICE})
    endif ()
    # If URHO3D_DOCS build option is set then add the custom 'doc' target into the default 'all' target, i.e. a normal build would not only build the software but also the documentation
    if (URHO3D_DOCS)
        set (ALL ALL)
    endif ()

    # Add custom 'doc' target for generating Urho3D documentation
    if (URHO3D_UPDATE_SOURCE_TREE)
        foreach (FILE ScriptAPI.dox AngelScriptAPI.h LuaScriptAPI.dox)
            list (APPEND UPDATE_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_BINARY_DIR}/generated/${FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${FILE})
        endforeach ()
    endif ()
    file (GLOB DOX_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.dox)
    add_custom_command (OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/html/index.html
        COMMAND ${DOXYGEN_EXECUTABLE} generated/Doxyfile ${REDIRECT_STDOUT}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/generated/Doxyfile ${DOX_FILES} ${CMAKE_CURRENT_BINARY_DIR}/generated/ScriptAPI.dox ${CMAKE_CURRENT_BINARY_DIR}/generated/LuaScriptAPI.dox
        COMMENT "Generating documentation with Doxygen")
    add_custom_target (doc ${ALL}
        ${UPDATE_COMMANDS}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/html/index.html)
endif ()

# Make sure html output directory exists and not empty
file (WRITE ${CMAKE_CURRENT_BINARY_DIR}/html/Readme.txt "If the URHO3D_DOCS build option is set then the Urho3D documentation will be built automatically as part of the Urho3D library build. If build option is not set, however, then use 'doc' built-in target to generate the documentation manually (must do this before installing the SDK).\n")

# Currently it is not possible to make built-in 'install' target to depend on 'doc' in CMake, therefore 'make doc' command need to be invoked manually before 'make install' in order to install the SDK with complete documentation
# Unless, URHO3D_DOCS build option is set in which case the custom 'doc' target is part of the default 'all' target which in turn the 'install' target depends on, so a single 'make install' alone is suffice to install everything
install (DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html/ DESTINATION ${DEST_SHARE_DIR}/docs)

# Define post build steps
if (CMAKE_EXTRA_GENERATOR MATCHES Eclipse AND NOT CMAKE_HOST_WIN32)
    add_custom_command (TARGET doc
        COMMAND ${CMAKE_COMMAND} -E remove ~/.eclipse/`ls -1t ~/.eclipse |grep org.eclipse.platform_ |head -1`/plugins/html || true
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_BINARY_DIR}/html ~/.eclipse/`ls -1t ~/.eclipse |grep org.eclipse.platform_ |head -1`/plugins/html || true
        COMMENT "Creating symbolic link to HTML documentation in the Eclipse IDE")
endif ()
