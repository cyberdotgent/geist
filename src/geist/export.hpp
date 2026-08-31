// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32) && !defined(GEIST_STATIC)
#if defined(GEIST_BUILDING_DLL)
#define GEIST_API __declspec(dllexport)
#else
#define GEIST_API __declspec(dllimport)
#endif
#else
#define GEIST_API
#endif
