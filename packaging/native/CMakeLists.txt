cmake_minimum_required(VERSION 3.24)
project(VirtualNetworkAdapterSDK VERSION 0.1.1 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This SDK requires Windows x64. No driver is installed by this build.")
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(TitanVirtualNetworkAdapterSDK STATIC
  "sdk/SDK/NetworkAdapter.cpp"
)
add_library(VirtualNetworkAdapterSDK::sdk ALIAS TitanVirtualNetworkAdapterSDK)
set_target_properties(TitanVirtualNetworkAdapterSDK PROPERTIES EXPORT_NAME sdk)
target_compile_features(TitanVirtualNetworkAdapterSDK PUBLIC cxx_std_17)
target_compile_definitions(TitanVirtualNetworkAdapterSDK PUBLIC )
target_compile_definitions(TitanVirtualNetworkAdapterSDK PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
if(MSVC)
  target_compile_options(TitanVirtualNetworkAdapterSDK PRIVATE /utf-8 /EHsc /W4)
endif()
target_include_directories(TitanVirtualNetworkAdapterSDK PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>"
  "$<INSTALL_INTERFACE:include/>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/SDK>"
  "$<INSTALL_INTERFACE:include/SDK>"
)
target_link_libraries(TitanVirtualNetworkAdapterSDK PUBLIC advapi32 comctl32 comdlg32 crypt32 gdi32 newdev ole32 setupapi shell32 user32 winhttp wintrust)
install(TARGETS TitanVirtualNetworkAdapterSDK EXPORT VirtualNetworkAdapterSDKTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY sdk/ DESTINATION include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
install(FILES LICENSE DESTINATION share/virtual-network-adapter-sdk)
install(EXPORT VirtualNetworkAdapterSDKTargets NAMESPACE VirtualNetworkAdapterSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VirtualNetworkAdapterSDK)
configure_file("VirtualNetworkAdapterSDKConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/VirtualNetworkAdapterSDKConfig.cmake" @ONLY)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/VirtualNetworkAdapterSDKConfigVersion.cmake" VERSION 0.1.1 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/VirtualNetworkAdapterSDKConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/VirtualNetworkAdapterSDKConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VirtualNetworkAdapterSDK)
