// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPropertyFloatPacker.h"
#include "Helpers/PCGExCollectionPropertySetWriter.h"

class UPCGExAssetCollection;
struct FPCGExAssetCollectionEntry;

// Plain FPCGContext -- the mesh selector consumer runs inside a stock PCG element's context.
struct FPCGContext;

namespace PCGExCollections
{
	/**
	 * Collection-only glue over FPCGExPropertyFloatPacker, turning UPCGExAssetCollection* /
	 * FPCGExAssetCollectionEntry* into the lookups the generic packer expects.
	 *
	 *   Packer.Initialize(Context, Layout, Hosts);
	 *   for (... entries ...) { Packer.PackEntry(Entry, Host, Floats); }
	 *
	 * Prototype resolution is first-match-wins over Hosts, so pass a deterministically ordered
	 * list (see FPickUnpacker::GetCollectionsInStableOrder). A packed slot can only carry one
	 * type, so hosts that disagree on a property's type are warned about at Initialize -- the
	 * losers' entries would otherwise silently render the winner's default.
	 */
	class PCGEXCOLLECTIONS_API FPCGExCollectionPropertyFloatPacker
	{
	public:
		FPCGExCollectionPropertyFloatPacker() = default;

		/**
		 * Resolve widths and prototype values against the schemas Hosts declare. Safe to call with
		 * an empty layout. MaxFloats caps the packed width; see FPCGExPropertyFloatPacker.
		 * @return true if at least one property resolved to a packable slot.
		 */
		bool Initialize(
			FPCGContext* InContext,
			const FPCGExPackedFloatLayout& Layout,
			TConstArrayView<const UPCGExAssetCollection*> Hosts,
			int32 MaxFloats = MAX_int32);

		/** Fill OutFloats for one entry. Entry and Host may both be null (all slots default). */
		void PackEntry(const FPCGExAssetCollectionEntry* Entry, const UPCGExAssetCollection* Host, TArray<float>& OutFloats) const;

		int32 GetNumFloats() const { return Inner.GetNumFloats(); }

		bool HasOutputs() const { return Inner.HasOutputs(); }

#if WITH_EDITOR
		FString DescribeLayout() const { return Inner.DescribeLayout(); }
#endif

	protected:
		void WarnOnHostTypeConflicts(FPCGContext* InContext, TConstArrayView<const UPCGExAssetCollection*> Hosts) const;

		FCollectionPrototypeProvider Provider;
		FPCGExPropertyFloatPacker Inner;
	};
}
