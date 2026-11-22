# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

# API reference with Doxygen: the target ngui_api_docs writes HTML to
# <build>/api/html. Input are the public headers, including the generated
# ones in the build directory (ngui/Version.h), and the pages in docs/api
# (main page, namespaces). Every Doxygen warning fails the target, so the
# reference stays complete (CONVENTIONS.md, section 1).

find_package(Doxygen 1.9.8 REQUIRED)

set(DOXYGEN_PROJECT_NAME "NewGUI")
set(DOXYGEN_PROJECT_BRIEF "${PROJECT_DESCRIPTION}")
set(DOXYGEN_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/api")
set(DOXYGEN_GENERATE_LATEX NO)
set(DOXYGEN_FULL_PATH_NAMES YES)
set(DOXYGEN_STRIP_FROM_PATH
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/include")
set(DOXYGEN_STRIP_FROM_INC_PATH ${DOXYGEN_STRIP_FROM_PATH})
set(DOXYGEN_RECURSIVE YES)
set(DOXYGEN_FILE_PATTERNS "*.h" "*.dox")
set(DOXYGEN_EXTRACT_ALL NO)
set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)
set(DOXYGEN_QUIET YES)
# The tab width of the sources (CONVENTIONS.md, section 4.5).
set(DOXYGEN_TAB_SIZE 4)
# Reproducible output: no generation date in the page footers.
set(DOXYGEN_TIMESTAMP NO)

doxygen_add_docs(ngui_api_docs
    "${PROJECT_SOURCE_DIR}/docs/api"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_BINARY_DIR}/include"
    COMMENT "Generating the API reference with Doxygen")
