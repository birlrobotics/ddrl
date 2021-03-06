cmake_minimum_required(VERSION 2.8)

#Nom du projet
project(humanoid CXX C)

if(NOT DEFINED ROOT_DRL_PATH)
	set(ROOT_DRL_PATH "${CMAKE_SOURCE_DIR}/../..")
endif()

include(${ROOT_DRL_PATH}/common/cmake/Config.cmake)

find_package(CommonDRL)
find_package(ODEEnv)

include_directories(${CMAKE_SOURCE_DIR}/include ${COMMON_DRL_INCLUDE_DIRS} ${ODE_ENV_INCLUDE_DIRS})

file ( GLOB_RECURSE all_include include/*.hpp )
file ( GLOB all_sources src/*.cpp )

#-------------------
# Library Build
#-------------------
set(LIBRARY_NAME "humanoid")
rename_buildtype(LIBRARY_NAME)
add_library(${LIBRARY_NAME} SHARED ${all_include} ${all_sources})
target_link_libraries(${LIBRARY_NAME} ${COMMON_DRL_LIBRARY_LIGHT} ${ODE_ENV_LIBRARY})

#-------------------
# Personnals Test
#-------------------
add_executable(humanoid-env src/test/Humanoid.cpp ${all_include} ${all_sources})
target_link_libraries(humanoid-env ${COMMON_DRL_LIBRARY} ${ODE_ENV_LIBRARY})

if(APPLE)
	message(${all_sources})
	list(REMOVE_ITEM all_sources ${CMAKE_SOURCE_DIR}/src/HumanoidWorldView.cpp)
	message(${all_sources})
	add_executable(humanoid-env-osx src/test/HumanoidWorldViewOSX.cpp ${all_include} ${all_sources})
	target_link_libraries(humanoid-env-osx ${COMMON_DRL_LIBRARY_LIGHT} ${ODE_ENV_LIBRARY})
endif()

file(COPY data DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/)
file(COPY ${ROOT_DRL_PATH}/environment/ode-env/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/)

execute_process(COMMAND bash ${ROOT_DRL_PATH}/scripts/merge_ini.bash 
	${ROOT_DRL_PATH}/common/data/minimal_config.ini 
	${CMAKE_CURRENT_BINARY_DIR}/data/humanoid.ini 
	${CMAKE_CURRENT_BINARY_DIR}/config.ini)

#-------------------
# Unit Test
#-------------------
# set(all_test_source ${all_sources})
# enable_utest(all_test_source ${COMMON_DRL_LIBRARY_LIGHT})
