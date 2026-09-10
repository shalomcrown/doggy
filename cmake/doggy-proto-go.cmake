# Generate lora/internal/pb/doggy.pb.go from proto/doggy.proto.
# Used by the firmware CMakeLists and by operator-only packaging.

set(DOGGY_PROTO "${CMAKE_SOURCE_DIR}/proto/doggy.proto")
set(DOGGY_PB_GO "${CMAKE_SOURCE_DIR}/lora/internal/pb/doggy.pb.go")
set(DOGGY_PROTOC_GEN_GO "${CMAKE_BINARY_DIR}/protoc-gen-go")

if(NOT GO_EXECUTABLE)
    find_program(GO_EXECUTABLE go REQUIRED)
endif()

if(NOT DOGGY_PROTOC_CMD)
    find_program(DOGGY_PROTOC protoc REQUIRED)
    set(DOGGY_PROTOC_CMD "${DOGGY_PROTOC}")
endif()

add_custom_command(
    OUTPUT "${DOGGY_PROTOC_GEN_GO}"
    COMMAND "${CMAKE_COMMAND}" -E env
        GOBIN=${CMAKE_BINARY_DIR}
        GOFLAGS=
        "${GO_EXECUTABLE}" install google.golang.org/protobuf/cmd/protoc-gen-go@v1.34.2
    COMMENT "Installing protoc-gen-go"
)

add_custom_command(
    OUTPUT "${DOGGY_PB_GO}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_SOURCE_DIR}/lora/internal/pb"
    COMMAND "${DOGGY_PROTOC_CMD}"
        --plugin=protoc-gen-go=${DOGGY_PROTOC_GEN_GO}
        --go_out=${CMAKE_SOURCE_DIR}/lora
        --go_opt=module=doggy-lora
        -I ${CMAKE_SOURCE_DIR}/proto
        "${DOGGY_PROTO}"
    DEPENDS "${DOGGY_PROTO}" "${DOGGY_PROTOC_GEN_GO}"
    COMMENT "Generating Go protobuf sources"
)
add_custom_target(doggy_proto_go ALL DEPENDS "${DOGGY_PB_GO}")
