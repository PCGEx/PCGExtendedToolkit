// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FPrimitiveDrawInterface;
class FPCGExSketchEditController;
class UPCGExClusterSketchComponent;

/**
 * Renders a sketch edit controller's state through a bare PDI -- host-agnostic like the rest of the
 * controller seam, so the standalone viewport and any later level-viewport host draw identically.
 * Everything draws in SDPG_Foreground: this is an authoring overlay, never occluded by the scene.
 */
struct PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API FPCGExSketchDrawHelper
{
	/**
	 * What the host's MESH layer already covers, per kind -- this draws only what it does not. A host
	 * with no mesh layer (the standalone editor today) passes nothing and gets everything.
	 * Drag affordances (move ghost, connect preview, merge tether) and warning states (dormant stubs,
	 * degenerate edges, collocated vertices) are always drawn here -- transient or pathological states
	 * a mesh layer has no instance for.
	 */
	struct FMeshCoverage
	{
		bool bVertices = false;
		/** Reporting this OBLIGES the host to push the crossing edge set: the mesh layer, not this, then
		 *  carries the crossing tint. */
		bool bEdges = false;
		/** Hover is wrapped by a real outline mesh, so no hover chrome is needed here. */
		bool bHover = false;
		/** Crossing ghosts render as phantom-style meshes. */
		bool bGhosts = false;
	};

	static void Draw(const FPCGExSketchEditController& Controller, FPrimitiveDrawInterface* PDI, const FMeshCoverage& InCoverage);

	/** Host with no mesh layer: draws everything. An overload, not a default argument -- Clang rejects a
	 *  nested type's default member initializers being needed while the enclosing class is still open. */
	static void Draw(const FPCGExSketchEditController& Controller, FPrimitiveDrawInterface* PDI);

	/**
	 * The whole editing render pass for a host that carries a MESH LAYER: push the controller's live
	 * state into the component (which repaints its instances off it), then draw only what the meshes do
	 * not cover.
	 *
	 * Every editing host goes through here -- the standalone editor's preview-scene component and the
	 * in-level mode alike -- so the two cannot drift: a new state field, a new covered kind or a new
	 * affordance lands in both at once. A null component degrades to pure immediate mode.
	 */
	static void DrawWithComponent(const FPCGExSketchEditController& Controller, UPCGExClusterSketchComponent* Component, FPrimitiveDrawInterface* PDI);
};
