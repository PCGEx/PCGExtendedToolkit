// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCollectionSortKeys.h"

#include "Collections/PCGExLevelCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Hash/Blake3.h"
#include "Helpers/PCGExArchiveBlake3.h"
#include "UObject/PropertyPortFlags.h"

namespace PCGExSharedCompact
{
	FString MeshSortKey(const FPCGExMeshCollectionEntry& E)
	{
		// Descriptors key on their text delta from default instances, so a member the engine adds at its default never
		// moves an entry. The binary digest behind it only splits equal delta texts (floats export as %f).
		FSoftISMComponentDescriptor ISMDefaults;
		ISMDefaults.Hash = E.ISMDescriptor.Hash; // Cached CRC of every member: engine-dependent, kept out of the delta.
		const FPCGExStaticMeshDescriptorExtras ExtrasDefaults;

		FString DeltaText;
		FSoftISMComponentDescriptor::StaticStruct()->ExportText(DeltaText, &E.ISMDescriptor, &ISMDefaults, nullptr, PPF_None, nullptr);
		FPCGExStaticMeshDescriptorExtras::StaticStruct()->ExportText(DeltaText, &E.SMExtras, &ExtrasDefaults, nullptr, PPF_None, nullptr);

		// UTF-8, not TCHAR: sizeof(TCHAR) varies by platform.
		const FTCHARToUTF8 DeltaUtf8(*DeltaText);
		FBlake3 DeltaHasher;
		DeltaHasher.Update(DeltaUtf8.Get(), static_cast<uint64>(DeltaUtf8.Length()));

		FArchiveBlake3 BinaryHasher;
		FSoftISMComponentDescriptor::StaticStruct()->SerializeBin(
			BinaryHasher, const_cast<FSoftISMComponentDescriptor*>(&E.ISMDescriptor));
		FPCGExStaticMeshDescriptorExtras::StaticStruct()->SerializeBin(
			BinaryHasher, const_cast<FPCGExStaticMeshDescriptorExtras*>(&E.SMExtras));

		TStringBuilder<512> Builder;
		Builder.Append(E.StaticMesh.ToSoftObjectPath().ToString());
		Builder.Appendf(TEXT("|MV=%d|SI=%d|DS=%d|PCH=%u|DESC="),
		                static_cast<int32>(E.MaterialVariants),
		                E.SlotIndex,
		                static_cast<int32>(E.DescriptorSource),
		                E.PropertyComponentHash);
		Builder.Append(LexToString(DeltaHasher.Finalize()));
		Builder.Append(TEXT("|BIN="));
		Builder.Append(LexToString(BinaryHasher.Finalize()));

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

		// Tags discriminate groups (level-export identity). Lexical order keeps the key process-stable, the length
		// prefix keeps a tag's own text from reading as a second tag; an untagged entry adds no segment.
		if (!E.Tags.IsEmpty())
		{
			TArray<FName> SortedTags = E.Tags.Array();
			SortedTags.Sort(FNameLexicalLess());
			for (const FName& Tag : SortedTags)
			{
				const FString TagString = Tag.ToString();
				Builder.Appendf(TEXT("|TAG=%d:"), TagString.Len());
				Builder.Append(TagString);
			}
		}
		return FString(Builder.ToString());
	}

	FString LevelSortKey(const FPCGExLevelCollectionEntry& E)
	{
		return E.Level.ToSoftObjectPath().ToString();
	}
}
