cmake_minimum_required(VERSION 3.18)

set(SOLUTION_NAME EconomicDispatch)

project(${SOLUTION_NAME})
set(SOURCE_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(HOME_ROOT "$ENV{HOME}")
if (NOT HOME_ROOT)
	set(HOME_ROOT "$ENV{USERPROFILE}")
endif()
if (NOT HOME_ROOT AND DEFINED ENV{HOMEDRIVE} AND DEFINED ENV{HOMEPATH})
	set(HOME_ROOT "$ENV{HOMEDRIVE}$ENV{HOMEPATH}")
endif()
if (NOT HOME_ROOT)
	message(FATAL_ERROR "Could not determine home directory")
endif()
if (WIN32)
	string(REPLACE "\\" "/" HOME_ROOT "${HOME_ROOT}")
endif()

# natID 4.x installs the SDK as <home>/natID.SDK; 3.x put the same tree under
# <home>/Work. Accept either, and let Common.cmake derive NATID_SDK_BIN,
# NATID_SDK_LIB and NATID_SDK_INC from whichever one is present.
if (EXISTS "${HOME_ROOT}/natID.SDK/DevEnv/Common.cmake")
	set(NATID_SDK_ROOT ${HOME_ROOT}/natID.SDK)
elseif (EXISTS "${HOME_ROOT}/Work/DevEnv/Common.cmake")
	set(NATID_SDK_ROOT ${HOME_ROOT}/Work)
else()
	message(FATAL_ERROR
		"natID SDK not found. Looked in ${HOME_ROOT}/natID.SDK (4.x) and "
		"${HOME_ROOT}/Work (3.x)")
endif()
set(WORK_ROOT ${NATID_SDK_ROOT})

include(${NATID_SDK_ROOT}/DevEnv/Common.cmake)

include(${NATID_SDK_ROOT}/DevEnv/natGUI.cmake)

include(dispatch.cmake)

# macOS: the bundled Info.plist is an Xcode template, so its $(...) variables
# are left unexpanded by the Makefiles and Ninja generators and Finder refuses
# the bundle with "executable is missing". Expand them and ad-hoc sign, which
# arm64 requires in order to launch the app at all.
if (APPLE AND TARGET ${GUI_NAME})
	set(PLIST_TOOL /usr/libexec/PlistBuddy)
	set(BUNDLE_PLIST $<TARGET_BUNDLE_DIR:${GUI_NAME}>/Contents/Info.plist)
	add_custom_command(TARGET ${GUI_NAME} POST_BUILD
		COMMAND ${PLIST_TOOL} -c "Set :CFBundleExecutable ${GUI_NAME}" ${BUNDLE_PLIST}
		COMMAND ${PLIST_TOOL} -c "Set :CFBundleName ${GUI_NAME}" ${BUNDLE_PLIST}
		COMMAND ${PLIST_TOOL} -c "Set :CFBundleIdentifier com.example.dispatchgui" ${BUNDLE_PLIST}
		COMMAND ${PLIST_TOOL} -c "Set :CFBundlePackageType APPL" ${BUNDLE_PLIST}
		COMMAND ${PLIST_TOOL} -c "Set :CFBundleDevelopmentRegion en" ${BUNDLE_PLIST}
		COMMAND ${PLIST_TOOL} -c "Set :LSMinimumSystemVersion 12.3" ${BUNDLE_PLIST}
		COMMAND codesign --force --deep -s - $<TARGET_BUNDLE_DIR:${GUI_NAME}>
		COMMENT "Fixing Info.plist and signing the app bundle"
	)
endif()