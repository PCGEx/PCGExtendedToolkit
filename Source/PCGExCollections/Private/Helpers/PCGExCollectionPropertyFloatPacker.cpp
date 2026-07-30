// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExCollectionPropertyFloatPacker.h"

#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGExProperty.h"
#include "Core/PCGExAssetCollection.h"

namespace PCGExCollections
{
	bool FPCGExCollectionPropertyFloatPacker::Initialize(
		FPCGContext* InContext,
		const FPCGExPackedFloatLayout& Layout,
		TConstArrayView<const UPCGExAssetCollection*> Hosts,
		const int32 MaxFloats)
	{
		Provider.SetSearchOrder(Hosts);

		if (!Inner.Initialize(InContext, &Provider, Layout, MaxFloats))
		{
			return false;
		}

		WarnOnHostTypeConflicts(InContext, Hosts);
		return true;
	}

	void FPCGExCollectionPropertyFloatPacker::WarnOnHostTypeConflicts(FPCGContext* InContext, TConstArrayView<const UPCGExAssetCollection*> Hosts) const
	{
		for (int32 s = 0; s < Inner.GetNumSlots(); s++)
		{
			const FName PropertyName = Inner.GetSlotPropertyName(s);

			const UScriptStruct* Winner = nullptr;
			for (const UPCGExAssetCollection* Host : Hosts)
			{
				if (!Host)
				{
					continue;
				}

				const FInstancedStruct* Declared = Host->CollectionProperties.GetPropertyByName(PropertyName);
				if (!Declared || !Declared->IsValid())
				{
					continue;
				}

				if (!Winner)
				{
					Winner = Declared->GetScriptStruct();
					continue;
				}

				if (Declared->GetScriptStruct() != Winner)
				{
					PCGE_LOG_C(Warning, GraphAndLog, InContext,
					           FText::FromString(FString::Printf(TEXT("Property '%s' is declared with different types across the source collections; only '%s' is packed, and entries from the others fall back to its default."),
					                                             *PropertyName.ToString(), *Winner->GetName())));
					break;
				}
			}
		}
	}

	void FPCGExCollectionPropertyFloatPacker::PackEntry(const FPCGExAssetCollectionEntry* Entry, const UPCGExAssetCollection* Host, TArray<float>& OutFloats) const
	{
		Inner.Pack(OutFloats, [Entry, Host](const FName PropertyName)
		{
			return ResolveEntrySourceProperty(Entry, Host, PropertyName);
		});
	}
}
