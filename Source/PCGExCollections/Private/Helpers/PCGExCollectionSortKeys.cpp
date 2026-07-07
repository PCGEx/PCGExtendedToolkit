// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCollectionSortKeys.h"

#include "Collections/PCGExLevelCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Hash/Blake3.h"
#include "Helpers/PCGExArchiveBlake3.h"

namespace PCGExSharedCompact
{
	FString MeshSortKey(const FPCGExMeshCollectionEntry& E)
	{
		// Descriptors digested with Blake3 (256-bit) rather than CRC32. CRC32's 32-bit footprint
		// could collide between distinct descriptors and leave the sort tie-break undefined for
		// those pairs; Blake3 pushes collision probability to ~2^-128 so ordering is fully
		// determined in practice.
		FArchiveBlake3 DescHasher;
		FSoftISMComponentDescriptor::StaticStruct()->SerializeBin(
			DescHasher, const_cast<FSoftISMComponentDescriptor*>(&E.ISMDescriptor));
		FPCGExStaticMeshComponentDescriptor::StaticStruct()->SerializeBin(
			DescHasher, const_cast<FPCGExStaticMeshComponentDescriptor*>(&E.SMDescriptor));
		const FBlake3Hash DescHash = DescHasher.Finalize();

		TStringBuilder<512> Builder;
		Builder.Append(E.StaticMesh.ToSoftObjectPath().ToString());
		Builder.Appendf(TEXT("|MV=%d|SI=%d|DS=%d|PCH=%u|DESC="),
		                static_cast<int32>(E.MaterialVariants),
		                E.SlotIndex,
		                static_cast<int32>(E.DescriptorSource),
		                E.PropertyComponentHash);
		Builder.Append(LexToString(DescHash));

		for (const FPCGExMaterialOverrideSingleEntry& S : E.MaterialOverrideVariants)
		{
			Builder.Appendf(TEXT("|MOV=%d:"), S.Weight);
			Builder.Append(S.Material.ToSoftObjectPath().ToString());
		}
		for (const FPCGExMaterialOverrideCollection& V : E.MaterialOverrideVariantsList)
		{
			Builder.Appendf(TEXT("|MOL=%d"), V.Weight);
			for (const FPCGExMaterialOverrideEntry& O : V.Overrides)
			{
				Builder.Appendf(TEXT(",%d:"), O.SlotIndex);
				Builder.Append(O.Material.ToSoftObjectPath().ToString());
			}
		}
		return FString(Builder.ToString());
	}

	FString LevelSortKey(const FPCGExLevelCollectionEntry& E)
	{
		return E.Level.ToSoftObjectPath().ToString();
	}
}
