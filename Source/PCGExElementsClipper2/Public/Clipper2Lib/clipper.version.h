/*******************************************************************************
* Author    :  Angus Johnson                                                   *
* Date      :  12 October 2025                                                 *
* Website   :  https://www.angusj.com                                          *
* Copyright :  Angus Johnson 2010-2025                                         *
* Purpose   :  Core Clipper Library structures and functions                   *
* License   :  https://www.boost.org/LICENSE_1_0.txt                           *
*******************************************************************************/

#pragma once

#ifndef PCGEX_CLIPPER_H_VERSION
#define PCGEX_CLIPPER_H_VERSION

namespace PCGExClipper2Lib
{
	// Upstream release this copy is based on (exact base commit in CLIPPER2_VENDOR.md).
	constexpr auto CLIPPER2_VERSION = "2.0.1";

	// PCGEx patch level over that base; bump on every change to the vendored library and log it in CLIPPER2_VENDOR.md.
	constexpr int PCGEX_CLIPPER2_PATCH = 1;
}

#endif
