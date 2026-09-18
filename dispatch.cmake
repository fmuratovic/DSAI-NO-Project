set(CORE_NAME core)
set(CLI_NAME  cli)
set(GUI_NAME  dispatchGui)

set(GUI_STAGE 3 CACHE STRING "natGUI stage to build (0 = none, 1..4)")

file(GLOB CORE_SOURCES ${CMAKE_CURRENT_LIST_DIR}/core/*.cpp)
file(GLOB CORE_HEADERS ${CMAKE_CURRENT_LIST_DIR}/core/*.h)

add_library(${CORE_NAME} STATIC ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(${CORE_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/core)
target_compile_features(${CORE_NAME} PUBLIC cxx_std_17)

source_group("core" FILES ${CORE_SOURCES} ${CORE_HEADERS})

set(MATRIX_LIB_DEBUG   "${NATID_SDK_LIB}/MatrixD.lib")
set(MATRIX_LIB_RELEASE "${NATID_SDK_LIB}/Matrix.lib")

target_link_libraries(${CORE_NAME} PUBLIC
    debug     ${MU_LIB_DEBUG}   debug     ${MATRIX_LIB_DEBUG}
    optimized ${MU_LIB_RELEASE} optimized ${MATRIX_LIB_RELEASE})

file(GLOB CLI_SOURCES ${CMAKE_CURRENT_LIST_DIR}/cli/*.cpp)

add_executable(${CLI_NAME} ${CLI_SOURCES})
source_group("cli" FILES ${CLI_SOURCES})

target_link_libraries(${CLI_NAME} PRIVATE ${CORE_NAME})

setIDEPropertiesForExecutable(${CLI_NAME})
setPlatformDLLPath(${CLI_NAME})

if(NOT GUI_STAGE STREQUAL "0")

    set(GUI_MAIN ${CMAKE_CURRENT_LIST_DIR}/gui/STAGE${GUI_STAGE}_main.cpp)

    if(NOT EXISTS ${GUI_MAIN})
        message(FATAL_ERROR "GUI_STAGE=${GUI_STAGE} but ${GUI_MAIN} does not exist")
    endif()

    message(STATUS "Building GUI stage ${GUI_STAGE}: ${GUI_MAIN}")

    file(GLOB GUI_INC_TD     ${MY_INC}/td/*.h)
    file(GLOB GUI_INC_GUI    ${MY_INC}/gui/*.h)
    file(GLOB GUI_INC_CNT    ${MY_INC}/cnt/*.h)

    set(GUI_PLIST ${CMAKE_CURRENT_LIST_DIR}/res/appIcon/AppIcon.plist)
    if(WIN32)
        set(GUI_WINAPP_ICON ${CMAKE_CURRENT_LIST_DIR}/res/appIcon/winAppIcon.rc)
    else()
        set(GUI_WINAPP_ICON ${CMAKE_CURRENT_LIST_DIR}/res/appIcon/winAppIcon.cpp)
    endif()

    add_executable(${GUI_NAME}
        ${GUI_MAIN}
        ${GUI_INC_TD} ${GUI_INC_GUI} ${GUI_INC_CNT}
        ${GUI_WINAPP_ICON})

    source_group("gui"       FILES ${GUI_MAIN})
    source_group("inc\\td"   FILES ${GUI_INC_TD})
    source_group("inc\\gui"  FILES ${GUI_INC_GUI})
    source_group("inc\\cnt"  FILES ${GUI_INC_CNT})

    target_link_libraries(${GUI_NAME} PRIVATE ${CORE_NAME})
    target_link_libraries(${GUI_NAME} PRIVATE
        debug     ${MU_LIB_DEBUG}   debug     ${NATGUI_LIB_DEBUG}
        optimized ${MU_LIB_RELEASE} optimized ${NATGUI_LIB_RELEASE})

    setTargetPropertiesForGUIApp(${GUI_NAME} ${GUI_PLIST})
    setAppIcon(${GUI_NAME} ${CMAKE_CURRENT_LIST_DIR})
    setIDEPropertiesForGUIExecutable(${GUI_NAME} ${CMAKE_CURRENT_LIST_DIR})
    setPlatformDLLPath(${GUI_NAME})

endif()
