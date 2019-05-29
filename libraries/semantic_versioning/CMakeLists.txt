project(
  SemanticVersioning )

cmake_minimum_required(
  VERSION 2.8.12 )

file(
   GLOB
   HEADERS
   "./include/eosio/semantic_versioning/semantic_versioning.hpp" )

add_library(
   semantic_versioning
   "./semantic_versioning.cpp"
   ${HEADERS} )

target_include_directories(
   semantic_versioning
   PUBLIC
   "${CMAKE_CURRENT_SOURCE_DIR}/include"
   "${CMAKE_CURRENT_BINARY_DIR}/include" )

install(
   TARGETS
   semantic_versioning
   RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} )

install(
  DIRECTORY
  ${CMAKE_CURRENT_SOURCE_DIR}/include/eosio/semantic_versioning
  DESTINATION
  ${CMAKE_INSTALL_FULL_INCLUDEDIR}/eosio/semantic_versioning
  FILES_MATCHING PATTERN "*.hpp" )
