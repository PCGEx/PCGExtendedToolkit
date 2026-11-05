// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

/** Tables behind the Density Surface Sampler, built once per process from fixed seeds with integer-only math. They are an
 *  output contract: every scatter is laid out from them, and a checksum test pins their values. */
namespace PCGExBlueNoise
{
	/** Points in the progressive tile. Every rank prefix is evenly spread over the torus. */
	inline constexpr int32 TileNumPoints = 16384;

	/** Tile coordinates are in 1/TileUnits of the tile side. */
	inline constexpr int32 TileUnits = 65536;

	/** Side of the toroidal dither mask, in cells. */
	inline constexpr int32 DitherMaskSize = 64;

	/** Interleaved X, Y per tile point, in rank order. Waits for the build if it has not finished yet. */
	PCGEXELEMENTSSAMPLING_API TConstArrayView<uint16> GetTileXY();

	/** Rank of each mask cell, row-major: a permutation of [0, DitherMaskSize^2). Waits for the build if needed. */
	PCGEXELEMENTSSAMPLING_API TConstArrayView<uint16> GetDitherMask();

	/** Starts the build on a background task. The module calls it at startup. */
	PCGEXELEMENTSSAMPLING_API void PrebuildTables();

	/** Waits for a started build. The module calls it at shutdown. */
	PCGEXELEMENTSSAMPLING_API void WaitForPrebuild();

	/** Farthest-point order over a fixed-seed random pool, as interleaved X, Y. Holds TileNumPoints * 2 values. */
	PCGEXELEMENTSSAMPLING_API void BuildTile(TArrayView<uint16> OutTileXY);

	/** Void-and-cluster ranks on a torus. Holds DitherMaskSize^2 values. */
	PCGEXELEMENTSSAMPLING_API void BuildMask(TArrayView<uint16> OutMask);
}
