# beken_robot AP — application base. Source list lives in ap_sources.cmake so
# secureboot_ai can compile the same tree with APP_ROOT pointed here.
set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}")
include(${CMAKE_CURRENT_LIST_DIR}/ap_sources.cmake)

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES ${priv_req})

if(CONFIG_MUSE_GADGET)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
    )

    # noise_core's PsaCryptoBackend embeds mbedtls_entropy_context and
    # mbedtls_ctr_drbg_context by value; their layouts depend on the same
    # MBEDTLS_CONFIG_FILE the psa_mbedtls library was built with. Without
    # it these sources fall back to the default mbedtls_config.h, the
    # structs come out smaller, and the library's field writes land on
    # adjacent stack data (ctr_drbg mutex.id got poisoned with
    # 0xffffffcc, then vQueueDelete faulted and reset the chip). The
    # component's PUBLIC define does not reach these sources through
    # armino PRIV_REQUIRES, so set it here explicitly.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        MBEDTLS_CONFIG_FILE="$ENV{ARMINO_AP_DIR}/components/psa_mbedtls/mbedtls_port/configs/mbedtls_psa_crypto_config.h"
    )
endif()

if(CONFIG_ROBOT_TEST)
    component_compile_definitions(ROBOT_TEST=1)
endif()
