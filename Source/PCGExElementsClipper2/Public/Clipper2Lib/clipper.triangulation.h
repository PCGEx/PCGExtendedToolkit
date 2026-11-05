/*******************************************************************************
* Author    :  Angus Johnson                                                   *
* Date      :  6 December 2025                                                 *
* Release   :  BETA RELEASE                                                    *
* Website   :  https://www.angusj.com                                          *
* Copyright :  Angus Johnson 2010-2025                                         *
* Purpose   :  Delaunay Triangulation                                          *
* License   :  https://www.boost.org/LICENSE_1_0.txt                           *
*******************************************************************************/

#pragma once

#include "CoreMinimal.h"

#ifndef PCGEX_CLIPPER_H_TRIANGULATION
#define PCGEX_CLIPPER_H_TRIANGULATION

#include <stack>
#include "Clipper2Lib/clipper.core.h"
#include "Clipper2Lib/clipper.engine.h"

namespace PCGExClipper2Lib
{
	enum class TriangulateResult
	{
		success,
		fail,
		no_polygons,
		paths_intersect
	};

	// Triangulate - this function will not accept intersecting paths
	// Note: For paths with holes, use TriangulateWithHoles instead
	PCGEXELEMENTSCLIPPER2_API
	TriangulateResult Triangulate(const Paths64& pp, Paths64& solution, bool useDelaunay = true);

	PCGEXELEMENTSCLIPPER2_API
	TriangulateResult Triangulate(const PathsD& pp, int decPlaces, PathsD& solution, bool useDelaunay = true);

	/**
	 * Triangulate paths with automatic hole detection using PolyTree.
	 * This function uses a Union operation to build the proper parent-child
	 * relationships between paths, identifying which paths are holes inside others.
	 * 
	 * @param pp Input paths (can contain overlapping or nested polygons)
	 * @param solution Output triangles (each path is a 3-point triangle)
	 * @param fillRule Fill rule for determining inside/outside (default: EvenOdd)
	 * @param useDelaunay If true, produces Delaunay-conforming triangulation
	 * @return TriangulateResult indicating success or failure reason
	 */
	PCGEXELEMENTSCLIPPER2_API
	TriangulateResult TriangulateWithHoles(
		const Paths64& pp,
		Paths64& solution,
		FillRule fillRule = FillRule::EvenOdd,
		bool useDelaunay = true,
		ZCallback64 zCallback = nullptr);

	PCGEXELEMENTSCLIPPER2_API
	TriangulateResult TriangulateWithHoles(
		const PathsD& pp,
		int decPlaces,
		PathsD& solution,
		FillRule fillRule = FillRule::EvenOdd,
		bool useDelaunay = true);

	/**
	 * Triangulate a PolyTree structure directly.
	 * Use this when you already have a PolyTree from a boolean operation.
	 * 
	 * @param polytree The PolyTree with established parent-child relationships
	 * @param solution Output triangles
	 * @param useDelaunay If true, produces Delaunay-conforming triangulation
	 * @return TriangulateResult indicating success or failure reason
	 */
	PCGEXELEMENTSCLIPPER2_API
	TriangulateResult TriangulatePolyTree(
		const PolyTree64& polytree,
		Paths64& solution,
		bool useDelaunay = true);

	PCGEXELEMENTSCLIPPER2_API
	TriangulateResult TriangulatePolyTreeD(
		const PolyTreeD& polytree,
		PathsD& solution,
		double scale,
		bool useDelaunay = true);
} // Clipper2Lib namespace

#endif
