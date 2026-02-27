// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExPointIO.h"

#include "Data/PCGPointArrayData.h"
#include "Metadata/Accessors/PCGCustomAccessor.h"
#include "Core/PCGExContext.h"
#include "PCGParamData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGExArrayHelpers.h"

namespace PCGExData
{
#pragma region FPointIO

	FPointIO::FPointIO(const TWeakPtr<FPCGContextHandle>& InContextHandle)
		: ContextHandle(InContextHandle), In(nullptr)
	{
	}

	FPointIO::FPointIO(const TWeakPtr<FPCGContextHandle>& InContextHandle, const UPCGBasePointData* InData)
		: ContextHandle(InContextHandle), In(InData)
	{
	}

	FPointIO::FPointIO(const TSharedRef<FPointIO>& InPointIO)
		: ContextHandle(InPointIO->GetContextHandle()), In(InPointIO->GetIn())
	{
		RootIO = InPointIO;

		TSet<FString> TagDump;
		InPointIO->Tags->DumpTo(TagDump); // Not ideal.

		Tags = MakeShared<FTags>(TagDump);
	}

	FPCGExContext* FPointIO::GetContext() const
	{
		const FPCGContext::FSharedContext<FPCGExContext> SharedContext(GetContextHandle());
		return SharedContext.Get();
	}

	void FPointIO::SetInfos(const int32 InIndex, const FName InOutputPin, const TSet<FString>* InTags)
	{
		IOIndex = InIndex;
		OutputPin = InOutputPin;
		NumInPoints = In ? In->GetNumPoints() : 0;

		if (InTags) { Tags = MakeShared<FTags>(*InTags); }
		else if (!Tags) { Tags = MakeShared<FTags>(); }
	}

	bool FPointIO::InitializeOutput(const EIOInit InitOut)
	{
		PCGEX_SHARED_CONTEXT(ContextHandle)

		if (LastInit == InitOut) { return true; }

		// Fast path: if we already have forwarded output, just update the mode.
		if (InitOut == EIOInit::Forward && IsValid(Out) && Out == In)
		{
			LastInit = EIOInit::Forward;
			return true;
		}

		// If we already duplicated and now want "New", just clear the points
		// instead of destroying and recreating the UObject (cheaper).
		if (LastInit == EIOInit::Duplicate && InitOut == EIOInit::New && Out && Out != In)
		{
			LastInit = EIOInit::New;
			Out->SetNumPoints(0);
			return true;
		}

		LastInit = InitOut;

		// Destroy previous output if it's our own (not the forwarded input).
		if (IsValid(Out) && Out != In)
		{
			SharedContext.Get()->ManagedObjects->Destroy(Out);
			Out = nullptr;
		}

		OutKeys.Reset();
		bMutable = false;

		// NoInit: read-only, no output will be produced.
		if (InitOut == EIOInit::NoInit)
		{
			Out = nullptr;
			return true;
		}

		// Forward: output IS the input (pass-through). Data is not owned/mutable.
		if (InitOut == EIOInit::Forward)
		{
			check(In);
			Out = const_cast<UPCGBasePointData*>(In);
			return true;
		}

		bMutable = true;

		// New: create a fresh point data of the same class as input (preserves metadata
		// schema via InitializeFromDataWithParams, but starts with zero points).
		if (InitOut == EIOInit::New)
		{
			if (In)
			{
				UObject* GenericInstance = SharedContext.Get()->ManagedObjects->New<UObject>(GetTransientPackage(), In->GetClass());
				if (!GenericInstance) { return false; }

				Out = Cast<UPCGBasePointData>(GenericInstance);
				check(Out)

				PCGExPointArrayDataHelpers::InitEmptyNativeProperties(In, Out);

				FPCGInitializeFromDataParams InitializeFromDataParams(In);
				InitializeFromDataParams.bInheritSpatialData = false;
				Out->InitializeFromDataWithParams(InitializeFromDataParams);
			}
			else
			{
				Out = SharedContext.Get()->ManagedObjects->New<UPCGPointArrayData>();
				if (!Out) { return false; }
			}

			return true;
		}

		// Duplicate: deep copy of input data including points and metadata.
		if (InitOut == EIOInit::Duplicate)
		{
			check(In)
			Out = SharedContext.Get()->ManagedObjects->DuplicateData<UPCGBasePointData>(In);
		}

		return Out != nullptr;
	}

	const UPCGBasePointData* FPointIO::GetOutIn(EIOSide& OutSide) const
	{
		if (Out)
		{
			OutSide = EIOSide::Out;
			return Out;
		}

		if (In)
		{
			OutSide = EIOSide::In;
			return In;
		}

		return nullptr;
	}

	const UPCGBasePointData* FPointIO::GetInOut(EIOSide& OutSide) const
	{
		if (In)
		{
			OutSide = EIOSide::In;
			return In;
		}

		if (Out)
		{
			OutSide = EIOSide::Out;
			return Out;
		}

		return nullptr;
	}

	bool FPointIO::GetSource(const UPCGData* InData, EIOSide& OutSide) const
	{
		if (!InData) { return false; }

		if (InData == In)
		{
			OutSide = EIOSide::In;
			return true;
		}

		if (InData == Out)
		{
			OutSide = EIOSide::Out;
			return true;
		}

		return false;
	}

	FPCGExTaggedData FPointIO::GetTaggedData(const EIOSide Source, const int32 InIdx) { return FPCGExTaggedData(GetData(Source), InIdx == INDEX_NONE ? IOIndex : InIdx, Tags, GetInKeys()); }

	void FPointIO::InitializeMetadataEntries_Unsafe(const bool bConservative) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPointIO::InitializeMetadataEntries);

		check(Out)

		UPCGMetadata* Metadata = Out->Metadata;
		TPCGValueRange<int64> MetadataEntries = Out->GetMetadataEntryValueRange(true);

		if (bConservative)
		{
			// Conservative: only allocate new metadata entries for points that don't
			// already have valid keys (invalid or stale parent references). This preserves
			// existing metadata entries, which is important when the output was duplicated
			// from input and some points already carry valid attribute data.
			TArray<int64*> KeysNeedingInit;
			KeysNeedingInit.Reserve(MetadataEntries.Num());
			const int64 ItemKeyOffset = Metadata->GetItemKeyCountForParent();

			for (int64& Key : MetadataEntries)
			{
				if (Key == PCGInvalidEntryKey || Key < ItemKeyOffset) { KeysNeedingInit.Add(&Key); }
			}

			if (KeysNeedingInit.Num() > 0) { Metadata->AddEntriesInPlace(KeysNeedingInit); }
		}
		else
		{
			// Non-conservative: replace ALL metadata entries with fresh ones.
			// Old keys are preserved as parent references in DelayedEntries so attribute
			// values can still be inherited/interpolated from the original data.
			const int32 NumEntries = MetadataEntries.Num();
			TArray<TTuple<int64, int64>> DelayedEntries;
			DelayedEntries.SetNum(NumEntries);

			for (int i = 0; i < NumEntries; i++)
			{
				int64 OldKey = MetadataEntries[i];
				MetadataEntries[i] = Metadata->AddEntryPlaceholder();
				DelayedEntries[i] = MakeTuple(MetadataEntries[i], OldKey);
			}

			Metadata->AddDelayedEntries(DelayedEntries);
		}
	}

	TSharedPtr<IPCGAttributeAccessorKeys> FPointIO::GetInKeys()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPointIO::GetInKeys);

		{
			FReadScopeLock ReadScopeLock(InKeysLock);
			if (InKeys) { return InKeys; }
		}

		{
			FWriteScopeLock WriteScopeLock(InKeysLock);
			if (InKeys) { return InKeys; }
			if (const TSharedPtr<FPointIO> PinnedRoot = RootIO.Pin()) { InKeys = PinnedRoot->GetInKeys(); }
			else { InKeys = MakeShared<FPCGAttributeAccessorKeysPointIndices>(In); }
		}

		return InKeys;
	}

	TSharedPtr<IPCGAttributeAccessorKeys> FPointIO::GetOutKeys(const bool bEnsureValidKeys)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPointIO::GetOutKeys);

		check(Out)

		{
			FReadScopeLock ReadScopeLock(OutKeysLock);
			if (OutKeys) { return OutKeys; }
		}

		{
			FWriteScopeLock WriteScopeLock(OutKeysLock);
			if (OutKeys) { return OutKeys; }

			if (bEnsureValidKeys) { InitializeMetadataEntries_Unsafe(true); }
			OutKeys = MakeShared<FPCGAttributeAccessorKeysPointIndices>(Out, false);
		}

		return OutKeys;
	}

	FScope FPointIO::GetInScope(const int32 Start, const int32 Count, const bool bInclusive) const
	{
		FScope Scope = bInclusive ? FScope(In, Start, Count) : FScope(In, Start + 1, Count - 1);
		return Scope;
	}

	FScope FPointIO::GetOutScope(const int32 Start, const int32 Count, const bool bInclusive) const
	{
		FScope Scope = bInclusive ? FScope(Out, Start, Count) : FScope(Out, Start + 1, Count - 1);
		return Scope;
	}

	FScope FPointIO::GetInRange(const int32 Start, const int32 End, const bool bInclusive) const
	{
		if (Start < End)
		{
			FScope Scope = bInclusive ? FScope(In, Start, End - Start) : FScope(In, Start + 1, (End - Start) - 2);
			return Scope;
		}
		FScope Scope = bInclusive ? FScope(In, End, Start - End) : FScope(In, End + 1, (Start - End) - 2);
		return Scope;
	}

	FScope FPointIO::GetOutRange(const int32 Start, const int32 End, const bool bInclusive) const
	{
		if (Start < End)
		{
			FScope Scope = bInclusive ? FScope(Out, Start, End - Start) : FScope(Out, Start + 1, (End - Start) - 2);
			return Scope;
		}
		FScope Scope = bInclusive ? FScope(Out, End, Start - End) : FScope(Out, End + 1, (Start - End) - 2);
		return Scope;
	}

	void FPointIO::SetPoints(const TArray<FPCGPoint>& InPCGPoints)
	{
		check(Out)
		Out->SetNumPoints(InPCGPoints.Num());
		SetPoints(0, InPCGPoints);
	}

	void FPointIO::SetPoints(const int32 StartIndex, const TArray<FPCGPoint>& InPCGPoints, const EPCGPointNativeProperties Properties)
	{
		check(Out)

#define PCGEX_COPYRANGEIF(_NAME, _TYPE, ...)\
		if (EnumHasAllFlags(Properties, EPCGPointNativeProperties::_NAME)){\
			const TPCGValueRange<_TYPE> Range = Out->Get##_NAME##ValueRange(false);\
			for(int i = 0; i < InPCGPoints.Num(); i++){ Range[StartIndex + i] = InPCGPoints[i]._NAME;}\
		}

		PCGEX_FOREACH_POINT_NATIVE_PROPERTY(PCGEX_COPYRANGEIF)
	}

	TArray<int32>& FPointIO::GetIdxMapping(const int32 NumElements)
	{
		check(Out)
		const int32 ExpectedNumElement = NumElements < 0 ? Out->GetNumPoints() : NumElements;

		{
			FReadScopeLock ReadScopeLock(IdxMappingLock);
			if (IdxMapping.IsValid())
			{
				check(IdxMapping->Num() == ExpectedNumElement)
				return *IdxMapping.Get();
			}
		}

		{
			FWriteScopeLock WriteScopeLock(IdxMappingLock);
			if (IdxMapping.IsValid()) { return *IdxMapping.Get(); }

			IdxMapping = MakeShared<TArray<int32>>();
			IdxMapping->SetNumUninitialized(ExpectedNumElement);

			return *IdxMapping.Get();
		}
	}

	void FPointIO::ClearIdxMapping()
	{
		IdxMapping.Reset();
	}

	void FPointIO::ConsumeIdxMapping(const EPCGPointNativeProperties Properties, const bool bClear)
	{
		check(IdxMapping.IsValid());
		check(IdxMapping->Num() == Out->GetNumPoints());

		InheritProperties(*IdxMapping.Get(), Properties);
		if (bClear) { ClearIdxMapping(); }
	}

	void FPointIO::InheritProperties(const int32 ReadStartIndex, const int32 WriteStartIndex, const int32 Count, const EPCGPointNativeProperties Properties) const
	{
		In->CopyPropertiesTo(Out, ReadStartIndex, WriteStartIndex, Count, Properties & In->GetAllocatedProperties());
	}

	void FPointIO::InheritProperties(const TArrayView<const int32>& ReadIndices, const TArrayView<const int32>& WriteIndices, const EPCGPointNativeProperties Properties) const
	{
		In->CopyPropertiesTo(Out, ReadIndices, WriteIndices, Properties & In->GetAllocatedProperties());
	}

	void FPointIO::InheritProperties(const TArrayView<const int32>& ReadIndices, const EPCGPointNativeProperties Properties) const
	{
		check(Out->GetNumPoints() >= ReadIndices.Num())

		TArray<int32> WriteIndices;
		PCGExArrayHelpers::ArrayOfIndices(WriteIndices, ReadIndices.Num());

		In->CopyPropertiesTo(Out, ReadIndices, WriteIndices, Properties & In->GetAllocatedProperties());
	}

	void FPointIO::InheritPoints(const int32 ReadStartIndex, const int32 WriteStartIndex, const int32 Count) const
	{
		In->CopyPointsTo(Out, ReadStartIndex, WriteStartIndex, Count);
	}

	void FPointIO::InheritPoints(const TArrayView<const int32>& ReadIndices, const TArrayView<const int32>& WriteIndices) const
	{
		In->CopyPointsTo(Out, ReadIndices, WriteIndices);
	}

	int32 FPointIO::InheritPoints(const TArrayView<const int8>& Mask, const bool bInvert) const
	{
		check(In)
		check(Out)
		TArray<int32> WriteIndices;
		const int32 NumElements = PCGExArrayHelpers::ArrayOfIndices(WriteIndices, Mask, 0, bInvert);
		Out->SetNumPoints(WriteIndices.Num());
		InheritPoints(WriteIndices, 0);
		return NumElements;
	}

	int32 FPointIO::InheritPoints(const TBitArray<>& Mask, const bool bInvert) const
	{
		check(In)
		check(Out)
		TArray<int32> WriteIndices;
		const int32 NumElements = PCGExArrayHelpers::ArrayOfIndices(WriteIndices, Mask, 0, bInvert);
		Out->SetNumPoints(WriteIndices.Num());
		InheritPoints(WriteIndices, 0);
		return NumElements;
	}

	void FPointIO::InheritPoints(const TArrayView<const int32>& WriteIndices) const
	{
		check(In)
		check(Out)

		const int32 NumReads = In->GetNumPoints();
		check(NumReads >= WriteIndices.Num())

		TArray<int32> ReadIndices;
		PCGExArrayHelpers::ArrayOfIndices(ReadIndices, NumReads);

		const EPCGPointNativeProperties EnsureAllocated = In->GetAllocatedProperties();
		Out->AllocateProperties(EnsureAllocated);
		In->CopyPropertiesTo(Out, ReadIndices, WriteIndices, EnsureAllocated);
	}

	void FPointIO::InheritPoints(const TArrayView<const int32>& SelectedIndices, const int32 StartIndex, const EPCGPointNativeProperties Properties) const
	{
		check(In)
		check(Out)

		const int32 NewSize = StartIndex + SelectedIndices.Num();
		if (Out->GetNumPoints() < NewSize) { Out->SetNumPoints(NewSize); }

		TArray<int32> WriteIndices;
		PCGExArrayHelpers::ArrayOfIndices(WriteIndices, SelectedIndices.Num(), StartIndex);

		const EPCGPointNativeProperties EnsureAllocated = Properties & In->GetAllocatedProperties();
		Out->AllocateProperties(EnsureAllocated);
		In->CopyPropertiesTo(Out, SelectedIndices, WriteIndices, EnsureAllocated);
	}

	void FPointIO::RepeatPoint(const int32 ReadIndex, const TArrayView<const int32>& WriteIndices, const EIOSide ReadSide) const
	{
		TArray<int32> SelectedIndices;
		SelectedIndices.Init(ReadIndex, WriteIndices.Num());

		(ReadSide == EIOSide::In ? In : Out)->CopyPointsTo(Out, SelectedIndices, WriteIndices);
	}

	void FPointIO::RepeatPoint(const int32 ReadIndex, const int32 WriteIndex, const int32 Count, const EIOSide ReadSide) const
	{
		TArray<int32> SelectedIndices;
		SelectedIndices.Init(ReadIndex, Count);

		TArray<int32> WriteIndices;
		PCGExArrayHelpers::ArrayOfIndices(WriteIndices, Count, WriteIndex);

		(ReadSide == EIOSide::In ? In : Out)->CopyPointsTo(Out, SelectedIndices, WriteIndices);
	}

	void FPointIO::ClearCachedKeys()
	{
		InKeys.Reset();
		OutKeys.Reset();
	}

	FPointIO::~FPointIO() {};

	bool FPointIO::StageOutput(FPCGExContext* TargetContext) const
	{
		check(!bTransactional)

		if (!IsEnabled() || !Out || (!bAllowEmptyOutput && Out->IsEmpty())) { return false; }

		// Forward mode: output the original input data (before any PCGEx type conversion).
		// This ensures downstream nodes see the exact same UPCGData pointer, preserving
		// any non-point-data type information that ToPointData might have changed.
		if (LastInit == EIOInit::Forward && Out == In && OriginalIn)
		{
			TargetContext->StageOutput(const_cast<UPCGData*>(OriginalIn), OutputPin, bPinless ? EStaging::Pinless : EStaging::None, Tags->Flatten());
		}
		else
		{
			// Managed: we created this data, context must prevent GC until output is consumed.
			// Mutable: we own this data and can clean up consumable attributes on it.
			const EStaging Staging =
				(Out != In ? EStaging::Managed : EStaging::None) |
				(bMutable ? EStaging::Mutable : EStaging::None) |
				(bPinless ? EStaging::Pinless : EStaging::None);

			TargetContext->StageOutput(Out, OutputPin, Staging, Tags->Flatten());
		}

		return true;
	}

	bool FPointIO::StageOutput(FPCGExContext* TargetContext, const int32 MinPointCount, const int32 MaxPointCount) const
	{
		if (Out)
		{
			const int64 OutNumPoints = Out->GetNumPoints();
			if (OutNumPoints <= 0) { return false; }
			if (MinPointCount > 0 && OutNumPoints < MinPointCount) { return false; }
			if (MaxPointCount > 0 && OutNumPoints > MaxPointCount) { return false; }
			return StageOutput(TargetContext);
		}
		return false;
	}

	bool FPointIO::StageAnyOutput(FPCGExContext* TargetContext) const
	{
		if (!IsEnabled()) { return false; }

		if (bTransactional)
		{
			if (InitializationData)
			{
				UPCGData* MutableData = const_cast<UPCGData*>(InitializationData.Get());
				if (!MutableData) { return false; }

				const EStaging Staging = (bPinless ? EStaging::Pinless : EStaging::None);
				TargetContext->StageOutput(MutableData, OutputPin, Staging, Tags->Flatten());

				return true;
			}

			return false;
		}

		if (!Out || (!bAllowEmptyOutput && Out->IsEmpty())) { return false; }

		return StageOutput(TargetContext);
	}

	int32 FPointIO::Gather(const TArrayView<int32> InIndices) const
	{
		if (!Out) { return 0; }

		const int32 ReducedNum = InIndices.Num();

		// No-op if all points are kept.
		if (ReducedNum == Out->GetNumPoints()) { return ReducedNum; }

		// In-place compaction: shuffle selected points to the front of the output arrays,
		// then truncate. This avoids allocating a temporary copy of the point data.
		EPCGPointNativeProperties Allocated = In->GetAllocatedProperties() | Out->GetAllocatedProperties();
		Out->AllocateProperties(Allocated);

#define PCGEX_VALUERANGE_GATHER(_NAME, _TYPE, ...) \
if (EnumHasAnyFlags(Allocated, EPCGPointNativeProperties::_NAME)){ \
TPCGValueRange<_TYPE> Range = Out->Get##_NAME##ValueRange(); \
for (int i = 0; i < ReducedNum; i++){Range[i] = Range[InIndices[i]];}}

		PCGEX_FOREACH_POINT_NATIVE_PROPERTY(PCGEX_VALUERANGE_GATHER)

#undef PCGEX_VALUERANGE_GATHER

		Out->SetNumPoints(ReducedNum);
		return ReducedNum;
	}

	int32 FPointIO::Gather(const TArrayView<int8> InMask, const bool bInvert) const
	{
		TArray<int32> Indices;
		Indices.Reserve(InMask.Num());

		if (bInvert) { for (int i = 0; i < InMask.Num(); i++) { if (!InMask[i]) { Indices.Add(i); } } }
		else { for (int i = 0; i < InMask.Num(); i++) { if (InMask[i]) { Indices.Add(i); } } }

		return Gather(Indices);
	}

	int32 FPointIO::Gather(const TBitArray<>& InMask, const bool bInvert) const
	{
		TArray<int32> Indices;
		Indices.Reserve(InMask.Num());

		if (bInvert) { for (int i = 0; i < InMask.Num(); i++) { if (!InMask[i]) { Indices.Add(i); } } }
		else { for (int i = 0; i < InMask.Num(); i++) { if (InMask[i]) { Indices.Add(i); } } }

		return Gather(Indices);
	}

	void FPointIO::DeleteAttribute(const FPCGAttributeIdentifier& Identifier) const
	{
		if (!Out) { return; }

		{
			FWriteScopeLock WriteScopeLock(AttributesLock);
			if (PCGExMetaHelpers::HasAttribute(Out->Metadata, Identifier)) { Out->Metadata->DeleteAttribute(Identifier); }
		}
	}

	void FPointIO::DeleteAttribute(const FPCGMetadataAttributeBase* Attribute) const
	{
		const FPCGAttributeIdentifier Identifier(Attribute->Name, Attribute->GetMetadataDomain()->GetDomainID());
		DeleteAttribute(Identifier);
	}

	void FPointIO::GetDataAsProxyPoint(FProxyPoint& OutPoint, const EIOSide Side) const
	{
		const FBox BoundsBox = Side == EIOSide::In ? In->GetBounds() : Out->GetBounds();
		const FVector Extents = BoundsBox.GetExtent();
		OutPoint.Transform.SetLocation(BoundsBox.GetCenter());
		OutPoint.BoundsMin = -Extents;
		OutPoint.BoundsMax = Extents;
	}

	FPCGMetadataAttributeBase* FPointIO::FindMutableAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const
	{
		const UPCGBasePointData* Data = GetData(InSide);
		if (!Data || !PCGExMetaHelpers::HasAttribute(Data, InIdentifier)) { return nullptr; }
		return Data->Metadata->GetMutableAttribute(InIdentifier);
	}

	const FPCGMetadataAttributeBase* FPointIO::FindConstAttribute(const FPCGAttributeIdentifier& InIdentifier, const EIOSide InSide) const
	{
		const UPCGBasePointData* Data = GetData(InSide);
		if (!Data || !PCGExMetaHelpers::HasAttribute(Data, InIdentifier)) { return nullptr; }
		return Data->Metadata->GetConstAttribute(InIdentifier);
	}

#pragma endregion

#pragma region FPointIOCollection

	FPointIOCollection::FPointIOCollection(FPCGExContext* InContext, const bool bIsTransactional)
		: ContextHandle(InContext->GetOrCreateHandle()), bTransactional(bIsTransactional)
	{
	}

	FPointIOCollection::FPointIOCollection(FPCGExContext* InContext, const FName InputLabel, const EIOInit InitOut, const bool bIsTransactional)
		: FPointIOCollection(InContext, bIsTransactional)
	{
		TArray<FPCGTaggedData> Sources = InContext->InputData.GetInputsByPin(InputLabel);
		Initialize(Sources, InitOut);
	}

	FPointIOCollection::FPointIOCollection(FPCGExContext* InContext, TArray<FPCGTaggedData>& Sources, const EIOInit InitOut, const bool bIsTransactional)
		: FPointIOCollection(InContext, bIsTransactional)
	{
		Initialize(Sources, InitOut);
	}

	FPointIOCollection::~FPointIOCollection() {};

	void FPointIOCollection::Initialize(TArray<FPCGTaggedData>& Sources, const EIOInit InitOut)
	{
		PCGEX_SHARED_CONTEXT_VOID(ContextHandle)

		Pairs.Empty(Sources.Num());
		TSet<uint64> UniqueData;
		UniqueData.Reserve(Sources.Num());

		for (int i = 0; i < Sources.Num(); i++)
		{
			FPCGTaggedData& Source = Sources[i];
			bool bIsAlreadyInSet;
			UniqueData.Add(Source.Data->UID, &bIsAlreadyInSet);
			if (bIsAlreadyInSet) { continue; } // Dedupe

			const UPCGBasePointData* SourcePointData = PCGExPointIO::GetPointData(SharedContext.Get(), Source);
			if (!SourcePointData && bTransactional)
			{
				// Only allowed for execution-time only
				// Otherwise find a way to ensure conversion is plugged to the outputs, pin-less
				check(InitOut == EIOInit::NoInit)
				SourcePointData = PCGExPointIO::ToPointData(SharedContext.Get(), Source);
			}

			if (!SourcePointData || SourcePointData->IsEmpty()) { continue; }
			const TSharedPtr<FPointIO> NewIO = Emplace_GetRef(SourcePointData, InitOut, &Source.Tags);
			NewIO->OriginalIn = Source.Data;
			NewIO->bTransactional = bTransactional;
			NewIO->InitializationIndex = i;
			NewIO->InitializationData = Source.Data;
		}
		UniqueData.Empty();
	}

	TSharedPtr<FPointIO> FPointIOCollection::Emplace_GetRef(const UPCGBasePointData* In, const EIOInit InitOut, const TSet<FString>* Tags)
	{
		FWriteScopeLock WriteLock(PairsLock);
		TSharedPtr<FPointIO> NewIO = Pairs.Add_GetRef(MakeShared<FPointIO>(ContextHandle, In));
		NewIO->SetInfos(Pairs.Num() - 1, OutputPin, Tags);
		if (!NewIO->InitializeOutput(InitOut)) { return nullptr; }
		return NewIO;
	}

	TSharedPtr<FPointIO> FPointIOCollection::Emplace_GetRef(const EIOInit InitOut)
	{
		FWriteScopeLock WriteLock(PairsLock);
		TSharedPtr<FPointIO> NewIO = Pairs.Add_GetRef(MakeShared<FPointIO>(ContextHandle));
		NewIO->SetInfos(Pairs.Num() - 1, OutputPin);
		if (!NewIO->InitializeOutput(InitOut)) { return nullptr; }
		return NewIO;
	}

	TSharedPtr<FPointIO> FPointIOCollection::Emplace_GetRef(const TSharedPtr<FPointIO>& PointIO, const EIOInit InitOut)
	{
		TSharedPtr<FPointIO> Branch = Emplace_GetRef(PointIO->GetIn(), InitOut);
		if (!Branch) { return nullptr; }

		Branch->Tags->Reset(PointIO->Tags);
		Branch->RootIO = PointIO;
		return Branch;
	}

	TSharedPtr<FPointIO> FPointIOCollection::Insert_Unsafe(const int32 Index, const TSharedPtr<FPointIO>& PointIO)
	{
		check(!Pairs[Index]) // should be an empty spot
		Pairs[Index] = PointIO;
		PointIO->SetInfos(Index, OutputPin);
		return PointIO;
	}

	bool FPointIOCollection::ContainsData_Unsafe(const UPCGData* InData) const
	{
		for (const TSharedPtr<FPointIO>& IO : Pairs) { if (IO && (IO->In == InData || IO->Out == InData)) { return true; } }
		return false;
	}

	TSharedPtr<FPointIO> FPointIOCollection::Add_Unsafe(const TSharedPtr<FPointIO>& PointIO)
	{
		PointIO->SetInfos(Pairs.Add(PointIO), OutputPin);
		return PointIO;
	}

	TSharedPtr<FPointIO> FPointIOCollection::Add(const TSharedPtr<FPointIO>& PointIO)
	{
		FWriteScopeLock WriteLock(PairsLock);
		return Add_Unsafe(PointIO);
	}

	void FPointIOCollection::Add_Unsafe(const TArray<TSharedPtr<FPointIO>>& IOs)
	{
		if (IOs.IsEmpty()) { return; }
		Pairs.Reserve(Pairs.Num() + IOs.Num());
		for (const TSharedPtr<FPointIO>& IO : IOs)
		{
			if (!IO) { continue; }
			Add_Unsafe(IO);
		}
	}

	void FPointIOCollection::Add(const TArray<TSharedPtr<FPointIO>>& IOs)
	{
		FWriteScopeLock WriteLock(PairsLock);
		Add_Unsafe(IOs);
	}

	void FPointIOCollection::OverrideTags(const TSharedPtr<FPointIO>& InFrom, const TSharedPtr<FPointIO>& InTo)
	{
		InTo->Tags->Reset(InFrom->Tags);
	}

	void FPointIOCollection::IncreaseReserve(const int32 InIncreaseNum)
	{
		FWriteScopeLock WriteLock(PairsLock);
		Pairs.Reserve(Pairs.Max() + InIncreaseNum);
	}

	int32 FPointIOCollection::StageOutputs()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPointIOCollection::StageOutputs);

		FWriteScopeLock WriteLock(PairsLock);

		PCGEX_SHARED_CONTEXT_RET(ContextHandle, 0)
		FPCGExContext* Context = SharedContext.Get();

		Sort();

		int32 NumStaged = 0;
		for (const TSharedPtr<FPointIO>& IO : Pairs) { if (IO) { NumStaged += IO->StageOutput(Context); } }

		return NumStaged;
	}

	int32 FPointIOCollection::StageOutputs(const int32 MinPointCount, const int32 MaxPointCount)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPointIOCollection::StageOutputsMinMax);

		FWriteScopeLock WriteLock(PairsLock);

		PCGEX_SHARED_CONTEXT_RET(ContextHandle, 0)
		FPCGExContext* Context = SharedContext.Get();

		Sort();

		int32 NumStaged = 0;
		for (const TSharedPtr<FPointIO>& IO : Pairs) { if (IO) { NumStaged += IO->StageOutput(Context, MinPointCount, MaxPointCount); } }

		return NumStaged;
	}

	int32 FPointIOCollection::StageAnyOutputs()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPointIOCollection::StageOutputsAny);

		FWriteScopeLock WriteLock(PairsLock);

		PCGEX_SHARED_CONTEXT_RET(ContextHandle, 0)
		FPCGExContext* Context = SharedContext.Get();

		Sort();

		int32 NumStaged = 0;
		for (const TSharedPtr<FPointIO>& IO : Pairs) { if (IO) { NumStaged += IO->StageAnyOutput(Context); } }

		return NumStaged;
	}

	void FPointIOCollection::Sort()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPointIOCollection::Sort);
		Pairs.Sort([](const TSharedPtr<FPointIO>& A, const TSharedPtr<FPointIO>& B) { return A->IOIndex < B->IOIndex; });
	}

	FBox FPointIOCollection::GetInBounds() const
	{
		FBox Bounds = FBox(ForceInit);
		for (const TSharedPtr<FPointIO>& IO : Pairs) { Bounds += IO->GetIn()->GetBounds(); }
		return Bounds;
	}

	FBox FPointIOCollection::GetOutBounds() const
	{
		FBox Bounds = FBox(ForceInit);
		for (const TSharedPtr<FPointIO>& IO : Pairs) { Bounds += IO->GetOut()->GetBounds(); }
		return Bounds;
	}

	int32 FPointIOCollection::GetInNumPoints() const
	{
		int32 Count = 0;
		for (const TSharedPtr<FPointIO>& IO : Pairs) { Count += IO->GetNum(); }
		return Count;
	}

	void FPointIOCollection::PruneNullEntries(const bool bUpdateIndices)
	{
		const int32 MaxPairs = Pairs.Num();
		int32 WriteIndex = 0;
		if (!bUpdateIndices)
		{
			for (int32 i = 0; i < MaxPairs; i++) { if (Pairs[i]) { Pairs[WriteIndex++] = Pairs[i]; } }
		}
		else
		{
			for (int32 i = 0; i < MaxPairs; i++)
			{
				if (Pairs[i])
				{
					Pairs[i]->IOIndex = WriteIndex;
					Pairs[WriteIndex++] = Pairs[i];
				}
			}
		}
		Pairs.SetNum(WriteIndex);
	}

	void FPointIOCollection::Flush()
	{
		Pairs.Empty();
	}

#pragma endregion

#pragma region FPointIOTaggedEntries

	void FPointIOTaggedEntries::Add(const TSharedRef<FPointIO>& Value)
	{
		Entries.AddUnique(Value);
		Value->Tags->Set(TagId, TagValue);
	}

	bool FPointIOTaggedDictionary::CreateKey(const TSharedRef<FPointIO>& PointIOKey)
	{
		PCGExDataId TagValue = PointIOKey->Tags->GetOrSet<int64>(TagIdentifier, PointIOKey->GetInOut()->GetUniqueID());
		if (TagMap.Contains(TagValue->Value)) { return false; }
		TagMap.Add(TagValue->Value, Entries.Add(MakeShared<FPointIOTaggedEntries>(PointIOKey, TagIdentifier, TagValue)));
		return true;
	}

	bool FPointIOTaggedDictionary::RemoveKey(const TSharedRef<FPointIO>& PointIOKey)
	{
		const PCGExDataId TagValue = PCGEX_GET_DATAIDTAG(PointIOKey->Tags, TagIdentifier);

		if (!TagValue) { return false; }

		const int32* Index = TagMap.Find(TagValue->Value);
		if (!Index) { return false; }

		Entries[*Index] = nullptr;
		TagMap.Remove(TagValue->Value);
		return true;
	}

	bool FPointIOTaggedDictionary::TryAddEntry(const TSharedRef<FPointIO>& PointIOEntry)
	{
		const PCGExDataId TagValue = PCGEX_GET_DATAIDTAG(PointIOEntry->Tags, TagIdentifier);
		if (!TagValue) { return false; }

		if (const int32* Index = TagMap.Find(TagValue->Value))
		{
			Entries[*Index]->Add(PointIOEntry);
			return true;
		}

		return false;
	}

	TSharedPtr<FPointIOTaggedEntries> FPointIOTaggedDictionary::GetEntries(const int32 Key)
	{
		if (const int32* Index = TagMap.Find(Key)) { return Entries[*Index]; }
		return nullptr;
	}

	void GetPoints(const FScope& Scope, TArray<FPCGPoint>& OutPCGPoints)
	{
		OutPCGPoints.Reserve(Scope.Count);

		const TConstPCGValueRange<FTransform> TransformRange = Scope.Data->GetConstTransformValueRange();
		const TConstPCGValueRange<float> SteepnessRange = Scope.Data->GetConstSteepnessValueRange();
		const TConstPCGValueRange<float> DensityRange = Scope.Data->GetConstDensityValueRange();
		const TConstPCGValueRange<FVector> BoundsMinRange = Scope.Data->GetConstBoundsMinValueRange();
		const TConstPCGValueRange<FVector> BoundsMaxRange = Scope.Data->GetConstBoundsMaxValueRange();
		const TConstPCGValueRange<FVector4> ColorRange = Scope.Data->GetConstColorValueRange();
		const TConstPCGValueRange<int64> MetadataEntryRange = Scope.Data->GetConstMetadataEntryValueRange();
		const TConstPCGValueRange<int32> SeedRange = Scope.Data->GetConstSeedValueRange();

		for (int i = 0; i < Scope.Count; i++)
		{
			const int32 Index = Scope.Start + i;
			FPCGPoint& Pt = OutPCGPoints.Emplace_GetRef(TransformRange[Index], DensityRange[Index], SeedRange[Index]);
			Pt.Steepness = SteepnessRange[Index];
			Pt.BoundsMin = BoundsMinRange[Index];
			Pt.BoundsMax = BoundsMaxRange[Index];
			Pt.Color = ColorRange[Index];
			Pt.MetadataEntry = MetadataEntryRange[Index];
		}
	}

	TSharedPtr<FPointIO> TryGetSingleInput(FPCGExContext* InContext, const FName InputPinLabel, const bool bTransactional, const bool bRequired)
	{
		TSharedPtr<FPointIO> SingleIO;
		const TSharedPtr<FPointIOCollection> Collection = MakeShared<FPointIOCollection>(InContext, InputPinLabel, EIOInit::NoInit, bTransactional);

		if (!Collection->Pairs.IsEmpty() && Collection->Pairs[0]->GetNum() > 0)
		{
			SingleIO = Collection->Pairs[0];
		}
		else if (bRequired)
		{
			PCGEX_LOG_MISSING_INPUT(InContext, FText::Format(FText::FromString(TEXT("Missing or zero-points '{0}' inputs")), FText::FromName(InputPinLabel)))
		}

		return SingleIO;
	}

	int32 PCGExPointIO::GetTotalPointsNum(const TArray<TSharedPtr<FPointIO>>& InIOs, const EIOSide InSide)
	{
		int32 TotalNum = 0;

		if (InSide == EIOSide::In)
		{
			for (const TSharedPtr<FPointIO>& IO : InIOs)
			{
				if (!IO || !IO->GetIn()) { continue; }
				TotalNum += IO->GetIn()->GetNumPoints();
			}
		}
		else
		{
			for (const TSharedPtr<FPointIO>& IO : InIOs)
			{
				if (!IO || !IO->GetOut()) { continue; }
				TotalNum += IO->GetOut()->GetNumPoints();
			}
		}

		return TotalNum;
	}

	const UPCGBasePointData* PCGExPointIO::GetPointData(const FPCGContext* Context, const FPCGTaggedData& Source)
	{
		const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Source.Data);
		if (!PointData) { return nullptr; }

		return PointData;
	}

	UPCGBasePointData* PCGExPointIO::GetMutablePointData(const FPCGContext* Context, const FPCGTaggedData& Source)
	{
		const UPCGBasePointData* PointData = GetPointData(Context, Source);
		if (!PointData) { return nullptr; }

		return const_cast<UPCGBasePointData*>(PointData);
	}

	const UPCGBasePointData* PCGExPointIO::ToPointData(FPCGExContext* Context, const FPCGTaggedData& Source)
	{
		// Converts non-point PCG data into point data for uniform processing.
		// WARNING: newly created data is transient and managed internally.
		// Do NOT duplicate or inherit from the result, as the parent data may be
		// destroyed when the context is torn down.
		if (const UPCGBasePointData* RealPointData = Cast<const UPCGBasePointData>(Source.Data))
		{
			return RealPointData;
		}

		// Spatial data (volumes, landscapes, etc.) → sample into points.
		if (const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(Source.Data))
		{
			const UPCGBasePointData* PointData = Cast<const UPCGSpatialData>(SpatialData)->ToPointData(Context);
			if (PointData != SpatialData) { Context->ManagedObjects->Add(const_cast<UPCGBasePointData*>(PointData)); }
			return PointData;
		}

		// Param data (key-value metadata without spatial info) → create identity-transform
		// points with one point per metadata entry, preserving all attributes.
		if (const UPCGParamData* ParamData = Cast<UPCGParamData>(Source.Data))
		{
			const UPCGMetadata* ParamMetadata = ParamData->Metadata;
			const int64 ParamItemCount = ParamMetadata->GetLocalItemCount();

			if (ParamItemCount != 0)
			{
				UPCGBasePointData* PointData = Cast<UPCGBasePointData>(Context->ManagedObjects->New<UPCGPointArrayData>());
				check(PointData->Metadata);
				PointData->Metadata->Initialize(ParamMetadata);
				PointData->SetNumPoints(ParamItemCount);
				PointData->AllocateProperties(EPCGPointNativeProperties::MetadataEntry);
				TPCGValueRange<int64> MetadataEntryRange = PointData->GetMetadataEntryValueRange(/*bAllocate=*/false);

				for (int PointIndex = 0; PointIndex < ParamItemCount; ++PointIndex) { MetadataEntryRange[PointIndex] = PointIndex; }

				return PointData;
			}
		}

		return nullptr;
	}

#pragma endregion
}
