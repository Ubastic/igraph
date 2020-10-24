include(GetGitRevisionDescription)

set(VERSION_FILE "${CMAKE_SOURCE_DIR}/IGRAPH_VERSION")
set(NEXT_VERSION_FILE "${CMAKE_SOURCE_DIR}/NEXT_VERSION")

git_describe(PACKAGE_VERSION)
if(PACKAGE_VERSION)
  if(EXISTS "${NEXT_VERSION_FILE}")
    file(READ "${NEXT_VERSION_FILE}" PACKAGE_VERSION)
    string(STRIP "${PACKAGE_VERSION}" PACKAGE_VERSION)
    get_git_head_revision(GIT_REFSPEC GIT_COMMIT_HASH)
    string(SUBSTRING "${GIT_COMMIT_HASH}" 0 8 GIT_COMMIT_HASH_SHORT)
	string(APPEND PACKAGE_VERSION "-dev+${GIT_COMMIT_HASH_SHORT}")
  endif()
  message(STATUS "Version number from Git: ${PACKAGE_VERSION}")
elseif(EXISTS "${VERSION_FILE}")
  file(READ "${VERSION_FILE}" PACKAGE_VERSION)
  string(STRIP "${PACKAGE_VERSION}" PACKAGE_VERSION)
  message(STATUS "Version number: ${PACKAGE_VERSION}")
else()
  message(FATAL "Cannot find out the version number of this package; IGRAPH_VERSION is missing.")
endif()

string(REGEX MATCH "^[^-]+" PACKAGE_VERSION_BASE "${PACKAGE_VERSION}")
string(
  REGEX REPLACE "^([0-9]+)\\.([0-9]+)\\.([0-9+])" "\\1;\\2;\\3"
  PACKAGE_VERSION_PARTS "${PACKAGE_VERSION_BASE}"
)
list(GET PACKAGE_VERSION_PARTS 0 PACKAGE_VERSION_MAJOR)
list(GET PACKAGE_VERSION_PARTS 1 PACKAGE_VERSION_MINOR)
list(GET PACKAGE_VERSION_PARTS 2 PACKAGE_VERSION_PATCH)

if(PACKAGE_VERSION MATCHES "^[^-]+-")
  string(
    REGEX REPLACE "^[^-]+-([^+]*)" "\\1" PACKAGE_VERSION_PRERELEASE "${PACKAGE_VERSION}"
  )
else()
  set(PACKAGE_VERSION_PRERELEASE "cmake-experimental")
endif()
