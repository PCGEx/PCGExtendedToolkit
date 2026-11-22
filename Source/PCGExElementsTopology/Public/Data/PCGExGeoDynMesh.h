// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/External/PCGExMesh.h"

namespace UE::Geometry
{
	class FDynamicMesh3;
}

namespace PCGExMesh
{
	/**
	 * Extracts geometry from a FDynamicMesh3 into FGeoMesh arrays.
	 * Sibling to FGeoStaticMesh but operates on dynamic meshes.
	 * Does NOT own the source mesh -- it must remain valid during extraction.
	 */
	class PCGEXELEMENTSTOPOLOGY_API FGeoDynMesh : public FGeoMesh
	{
	public:
		const UE::Geometry::FDynamicMesh3* SourceMesh = nullptr;

		FVector CWTolerance = FVector(DefaultVertexMergeHashTolerance);
		bool bPreciseVertexMerge = true;

		explicit FGeoDynMesh(
			const UE::Geometry::FDynamicMesh3* InMesh,
			const FVector& InCWTolerance = FVector(DefaultVertexMergeHashTolerance),
			bool bInPreciseVertexMerge = true);

		/** Edge-only extraction (for Raw, Boundaries modes). */
		void ExtractMeshSynchronous();

		/** Full triangulation with adjacency (for Dual, Hollow modes). */
		void TriangulateMeshSynchronous();

		/**
		 * Get averaged vertex colors, one entry per output vertex of the current triangulation
		 * (Dual/Hollow centroids get the average of their triangle's vertices).
		 * Must be called after extraction (and MakeDual/MakeHollowDual, if any).
		 * @return false if the mesh has no color overlay.
		 */
		bool GetAveragedVertexColors(TArray<FVector4f>& OutColors) const;

		/** @return number of UV layers on the source mesh, or 0 if no attributes. */
		int32 GetNumUVChannels() const;

		/**
		 * Get averaged vertex UVs for a specific channel, one entry per output vertex of the
		 * current triangulation (Dual/Hollow centroids get the average of their triangle's vertices).
		 * Must be called after extraction (and MakeDual/MakeHollowDual, if any).
		 * @return false if the channel doesn't exist.
		 */
		bool GetAveragedVertexUVs(int32 Channel, TArray<FVector2f>& OutUVs) const;

		/**
		 * Get averaged vertex normals, one entry per output vertex of the current triangulation
		 * (Dual/Hollow centroids get the average of their triangle's vertices). Prefers the primary
		 * normal overlay, falls back to per-vertex mesh normals. Values are not guaranteed normalized.
		 * Must be called after extraction (and MakeDual/MakeHollowDual, if any).
		 * @return false if the mesh has neither a normal overlay nor per-vertex normals.
		 */
		bool GetAveragedVertexNormals(TArray<FVector3f>& OutNormals) const;

	private:
		/** Maps sparse source vertex ID → dense output vertex index. Built during extraction. */
		TMap<int32, int32> VertexIDToDenseIndex;

		/** Dense vertex count at extraction time, before MakeDual/MakeHollowDual mutate the vertex array. */
		int32 DenseVertexCount = 0;

		/** Averages overlay elements per dense vertex (pre-dual index space). */
		template <typename OverlayType, typename ValueType>
		bool AverageOverlayPerVertex(const OverlayType* Overlay, TArray<ValueType>& OutValues) const;

		/** Remaps per-dense-vertex values to the current triangulation's output vertices (Dual/Hollow centroids). */
		template <typename ValueType>
		void RemapToTriangulation(TArray<ValueType>& Values) const;
	};
}
