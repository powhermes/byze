# Copyright (c) 2026 Byze developers
# Distributed under the MIT software license.

include(FetchContent)

function(add_liboqs)
  message("")
  message("Configuring liboqs (XMSS + SPHINCS+)...")

  if(NOT liboqs_POPULATED)
    FetchContent_Declare(
      liboqs
      GIT_REPOSITORY https://github.com/open-quantum-safe/liboqs.git
      GIT_TAG        0.14.0
      GIT_SHALLOW    TRUE
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(OQS_BUILD_ONLY_LIB ON CACHE BOOL "" FORCE)
    set(OQS_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(OQS_HAZARDOUS_EXPERIMENTAL_ENABLE_SIG_STFL_KEY_SIG_GEN ON CACHE BOOL "" FORCE)
    set(OQS_ENABLE_SIG_STFL_XMSS ON CACHE BOOL "" FORCE)
    set(OQS_ENABLE_SIG_STFL_xmss_sha256_h10 ON CACHE BOOL "" FORCE)
    set(OQS_ENABLE_SIG_ML_DSA ON CACHE BOOL "" FORCE)
    set(OQS_ENABLE_SIG_ml_dsa_44 ON CACHE BOOL "" FORCE)
    # v0.3: ML-DSA-44 (FIPS 204) is the stateless partner of SPHINCS+ for witness-v2 outputs
    set(OQS_MINIMAL_BUILD "SIG_sphincs_sha2_128s_simple;SIG_STFL_xmss_sha256_h10;SIG_ml_dsa_44" CACHE STRING "" FORCE)
    FetchContent_MakeAvailable(liboqs)
  endif()

  if(TARGET oqs)
    set_target_properties(oqs PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endfunction()
