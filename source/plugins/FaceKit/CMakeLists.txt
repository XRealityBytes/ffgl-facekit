set(FFGL_FACEKIT_SOURCES
    FaceKit.h
    FaceKit.cpp
    FaceMesh.h
    FaceMesh.cpp
    FaceModel.h
    FaceModel.cpp
    FaceMorphTargets.h
    FaceMorphTargets.cpp
    OscReceiver.h
    OscReceiver.cpp
    UdpSocket.h
    UdpSocket.cpp
)

add_library(ffgl-plugin-facekit MODULE ${FFGL_FACEKIT_SOURCES})

add_library(ffgl::plugin::facekit ALIAS ffgl-plugin-facekit)

target_compile_features(ffgl-plugin-facekit PRIVATE cxx_std_17)

target_include_directories(ffgl-plugin-facekit PRIVATE
    ${CMAKE_SOURCE_DIR}/deps
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ffgl-plugin-facekit PRIVATE ffgl::sdk)

if(WIN32)
    target_link_libraries(ffgl-plugin-facekit PRIVATE opengl32 ws2_32)
elseif(APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(ffgl-plugin-facekit PRIVATE ${OPENGL_FRAMEWORK})
endif()

set_target_properties(ffgl-plugin-facekit PROPERTIES
    BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/build/osx/FFGLPlugin-Info.plist"
    OUTPUT_NAME "FaceKit"
    PREFIX ""
)
