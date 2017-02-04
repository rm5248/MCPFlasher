cmake_minimum_required (VERSION 2.6)

option (CSERIAL_INSTALL_LOCATION
		"Install location for cserial library")
		
IF( NOT UNIX )
    SET(CSERIAL_INCLUDE_DIR         "${CSERIAL_INSTALL_LOCATION}/include"           CACHE STRING "Directory with CSerial include files")
    SET(CSERIAL_LIBRARIES           "${CSERIAL_INSTALL_LOCATION}/lib/cserial.lib"   CACHE STRING "CSerial library to link with")
ENDIF( NOT UNIX )

IF( UNIX )
    find_package(PkgConfig)
    pkg_search_module( CSERIAL REQUIRED cserial )
ENDIF( UNIX )

INCLUDE_DIRECTORIES(${CSERIAL_INCLUDE_DIR})

project (MCPFlasher)

add_executable( MCPFlasher MCPFlasher.c )
target_link_libraries( MCPFlasher ${CSERIAL_LIBRARIES})