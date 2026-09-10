# Laptop operator client: Go sidecar only. No Pi firmware, no MinGW.
# linux → DEB amd64; windows → NSIS (makensis on the Linux build host).

set(DOGGY_VERSION_BASE "1.0.0")

find_program(GO_EXECUTABLE go)
if(NOT GO_EXECUTABLE)
    message(FATAL_ERROR "go not found; install golang-go (see ./install-prereqs.sh)")
endif()

if(DOGGY_OPERATOR_OS STREQUAL "windows")
    set(DOGGY_LORA_GOOS windows)
    set(DOGGY_LORA_GOARCH amd64)
    set(DOGGY_LORA_BIN "${CMAKE_BINARY_DIR}/doggy-lora.exe")
elseif(DOGGY_OPERATOR_OS STREQUAL "linux")
    set(DOGGY_LORA_GOOS linux)
    set(DOGGY_LORA_GOARCH amd64)
    set(DOGGY_LORA_BIN "${CMAKE_BINARY_DIR}/doggy-lora")
else()
    message(FATAL_ERROR "DOGGY_OPERATOR_OS must be linux or windows")
endif()

include("${CMAKE_SOURCE_DIR}/cmake/doggy-proto-go.cmake")

file(GLOB_RECURSE DOGGY_LORA_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/lora/*.go"
    "${CMAKE_SOURCE_DIR}/lora/go.mod"
    "${CMAKE_SOURCE_DIR}/lora/go.sum"
)
add_custom_command(
    OUTPUT "${DOGGY_LORA_BIN}"
    COMMAND "${CMAKE_COMMAND}" -E env
        CGO_ENABLED=0
        GOOS=${DOGGY_LORA_GOOS}
        GOARCH=${DOGGY_LORA_GOARCH}
        "${GO_EXECUTABLE}" build -o "${DOGGY_LORA_BIN}" ./cmd/doggy-lora
    DEPENDS ${DOGGY_LORA_SOURCES} "${DOGGY_PB_GO}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/lora"
    COMMENT "Building doggy-lora (${DOGGY_LORA_GOOS}/${DOGGY_LORA_GOARCH})"
)
add_custom_target(doggy_lora ALL DEPENDS "${DOGGY_LORA_BIN}")

install(PROGRAMS "${DOGGY_LORA_BIN}" DESTINATION bin)
install(FILES
    "${CMAKE_SOURCE_DIR}/web/index.html"
    "${CMAKE_SOURCE_DIR}/web/rover.html"
    "${CMAKE_SOURCE_DIR}/web/lora.html"
    DESTINATION share/doggy
)

if(DOGGY_OPERATOR_OS STREQUAL "linux")
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/doggy-lora-operator.service"
            DESTINATION /etc/systemd/system)
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/doggy-lora.desktop"
            DESTINATION share/applications)
endif()

enable_testing()
add_test(
    NAME doggy-lora
    COMMAND "${GO_EXECUTABLE}" test ./...
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/lora"
)

set(CPACK_PACKAGE_VENDOR "Shalom Crown")
set(CPACK_PACKAGE_CONTACT "Shalom Crown <shalomcrown@cmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Doggy LoRa operator (localhost UI)")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Doggy LoRa")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/cpack-stamp.cmake")

if(DOGGY_OPERATOR_OS STREQUAL "windows")
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_PACKAGE_NAME "doggy-lora-operator")
    set(CPACK_MONOLITHIC_INSTALL ON)
    set(CPACK_NSIS_DISPLAY_NAME "Doggy LoRa")
    set(CPACK_NSIS_PACKAGE_NAME "Doggy LoRa")
    set(CPACK_NSIS_CONTACT "shalomcrown@cmail.com")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_INSTALL_ALL_USERS ON)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_DEFINES "RequestExecutionLevel admin")
    set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS
"DetailPrint 'Stopping Doggy LoRa service (if any)...'
nsExec::ExecToLog 'sc stop DoggyLoraOperator'
Sleep 500"
    )
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
"CreateDirectory '$PROGRAMDATA\\\\doggy'
nsExec::ExecToLog 'sc create DoggyLoraOperator start= auto DisplayName= \\\"Doggy LoRa\\\" binPath= \\\"$INSTDIR\\\\bin\\\\doggy-lora.exe --mode=operator --listen=127.0.0.1:8765 --web-root=$INSTDIR\\\\share\\\\doggy --config-file=$PROGRAMDATA\\\\doggy\\\\lora.json\\\"'
nsExec::ExecToLog 'sc description DoggyLoraOperator \\\"Doggy LoRa operator (localhost UI)\\\"'
nsExec::ExecToLog 'sc start DoggyLoraOperator'"
    )
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
"nsExec::ExecToLog 'sc stop DoggyLoraOperator'
nsExec::ExecToLog 'sc delete DoggyLoraOperator'"
    )
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
"CreateDirectory '$SMPROGRAMS\\\\Doggy LoRa'
CreateShortCut '$SMPROGRAMS\\\\Doggy LoRa\\\\Doggy LoRa.lnk' 'http://127.0.0.1:8765/'
CreateShortCut '$DESKTOP\\\\Doggy LoRa.lnk' 'http://127.0.0.1:8765/'"
    )
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
"Delete '$SMPROGRAMS\\\\Doggy LoRa\\\\Doggy LoRa.lnk'
RMDir '$SMPROGRAMS\\\\Doggy LoRa'
Delete '$DESKTOP\\\\Doggy LoRa.lnk'"
    )
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "shaloms-doggy-lora-operator")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_NAME "shaloms-doggy-lora-operator")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Shalom Crown <shalomcrown@cmail.com>")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Doggy LoRa operator (localhost UI on 127.0.0.1:8765)")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "systemd")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_SOURCE_DIR}/packaging/operator/debian/postinst;${CMAKE_SOURCE_DIR}/packaging/operator/debian/prerm;${CMAKE_SOURCE_DIR}/packaging/operator/debian/postrm"
    )
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()

include(CPack)
