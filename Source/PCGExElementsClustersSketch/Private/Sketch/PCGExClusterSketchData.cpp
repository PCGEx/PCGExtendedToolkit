// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExClusterSketchData.h"

#include "PCGExPropertyWriter.h"

#pragma region FPCGExSketchDataLayer

const FPCGExSketchDataRecord* FPCGExSketchDataLayer::FindRecord(const uint32 InId) const
{
	// An invalid id must never match a record carrying one: BuildRecordIndex admits such records.
	if (InId == PCGExSketch::InvalidRecordId)
	{
		return nullptr;
	}
	return Records.FindByPredicate([InId](const FPCGExSketchDataRecord& Record)
	{
		return Record.Id == InId;
	});
}

FPCGExSketchDataRecord* FPCGExSketchDataLayer::FindRecordMutable(const uint32 InId)
{
	if (InId == PCGExSketch::InvalidRecordId)
	{
		return nullptr;
	}
	return Records.FindByPredicate([InId](const FPCGExSketchDataRecord& Record)
	{
		return Record.Id == InId;
	});
}

const FInstancedStruct* FPCGExSketchDataLayer::ResolveEffectiveFrom(const FPCGExSketchDataRecord* InRecord, const FName InName) const
{
	return PCGExProperties::ResolveEffective(Schema, InRecord ? &InRecord->Values : nullptr, InName);
}

void FPCGExSketchDataLayer::BuildRecordIndex(TMap<uint32, int32>& OutIndex) const
{
	OutIndex.Reset();
	OutIndex.Reserve(Records.Num());
	for (int32 i = 0; i < Records.Num(); ++i)
	{
		// First wins, mirroring FindRecordIndex: a duplicate Id is unreachable either way.
		OutIndex.FindOrAdd(Records[i].Id, i);
	}
}

void FPCGExSketchDataLayer::BuildOutputConfigs(TArray<FPCGExPropertyOutputConfig>& OutConfigs) const
{
	OutConfigs.Reset();

	TArray<FPCGExPropertyResolved> Resolved;
	Schema.Resolve(Resolved);
	OutConfigs.Reserve(Resolved.Num());

	for (const FPCGExPropertyResolved& Entry : Resolved)
	{
		FPCGExPropertyOutputConfig& Config = OutConfigs.AddDefaulted_GetRef();
		Config.bEnabled = true;
		Config.PropertyName = Entry.Source->Name;
	}
}

uint32 FPCGExSketchDataLayer::AddRecord(const FName InLabel)
{
	if (NextRecordId == PCGExSketch::InvalidRecordId)
	{
		++NextRecordId;
	}
	FPCGExSketchDataRecord& Record = Records.AddDefaulted_GetRef();
	Record.Id = NextRecordId++;
	Record.Label = InLabel;
	Schema.ApplyToOverrides(Record.Values);
	return Record.Id;
}

int32 FPCGExSketchDataLayer::PurgeUnreferenced(const TConstArrayView<uint32> InLiveIds)
{
	if (Records.IsEmpty())
	{
		return 0;
	}

	TSet<uint32> Live;
	Live.Reserve(InLiveIds.Num());
	for (const uint32 Id : InLiveIds)
	{
		if (Id != PCGExSketch::InvalidRecordId)
		{
			Live.Add(Id);
		}
	}

	const int32 Before = Records.Num();
	Records.RemoveAll([&Live](const FPCGExSketchDataRecord& Record)
	{
		return !Live.Contains(Record.Id);
	});
	return Before - Records.Num();
}

int32 FPCGExSketchDataLayer::RepairRecordIds()
{
	// Counter first: it must clear every id already in use before any re-mint draws from it.
	for (const FPCGExSketchDataRecord& Record : Records)
	{
		if (Record.Id != PCGExSketch::InvalidRecordId && Record.Id >= NextRecordId)
		{
			NextRecordId = Record.Id + 1;
		}
	}

	TSet<uint32> Seen;
	Seen.Reserve(Records.Num());

	int32 NumRepaired = 0;
	for (FPCGExSketchDataRecord& Record : Records)
	{
		bool bAlreadySeen = false;
		if (Record.Id != PCGExSketch::InvalidRecordId)
		{
			Seen.Add(Record.Id, &bAlreadySeen);
		}

		if (Record.Id != PCGExSketch::InvalidRecordId && !bAlreadySeen)
		{
			continue;
		}

		// Re-minting only breaks refs that were already unreachable: the first holder wins every lookup.
		if (NextRecordId == PCGExSketch::InvalidRecordId)
		{
			++NextRecordId;
		}
		Record.Id = NextRecordId++;
		Seen.Add(Record.Id);
		++NumRepaired;
	}
	return NumRepaired;
}

#if WITH_EDITOR
void FPCGExSketchDataLayer::EDITOR_SyncRecordsToSchema()
{
	// Remap FIRST: a duplicated schema row aliases two entries in SyncToSchema's HeaderId index, which
	// silently drops one side's values.
	Schema.SyncAllSchemasAndRemap([this](TConstArrayView<FPCGExHeaderIdRemap> Remaps)
	{
		for (FPCGExSketchDataRecord& Record : Records)
		{
			Record.Values.ApplyHeaderIdRemap(Remaps);
		}
	});
	Schema.ReconcileImportOverrides();

	const TArray<FInstancedStruct> BuiltSchema = Schema.BuildSchema();
	for (FPCGExSketchDataRecord& Record : Records)
	{
		Record.Values.SyncToSchema(BuiltSchema);
	}
}
#endif

#pragma endregion

#pragma region FPCGExSketchData

int32 FPCGExSketchData::RepairRecordIds()
{
	return VertexLayer.RepairRecordIds() + EdgeLayer.RepairRecordIds();
}

#if WITH_EDITOR
void FPCGExSketchData::EDITOR_SyncAll()
{
	// Repair before syncing: only the first holder of an id is addressable, so a duplicate must be
	// resolved before anything resolves against it.
	RepairRecordIds();

	SketchProperties.SyncAllSchemas();
	SketchProperties.ReconcileImportOverrides();
	VertexLayer.EDITOR_SyncRecordsToSchema();
	EdgeLayer.EDITOR_SyncRecordsToSchema();
}
#endif

#pragma endregion
