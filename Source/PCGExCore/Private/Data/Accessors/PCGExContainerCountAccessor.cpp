// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Accessors/PCGExContainerCountAccessor.h"

#include "Data/PCGExData.h"
#include "Data/Buffers/PCGExBufferProperty.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataCommon.h"

namespace PCGExData
{
	namespace PCGExContainerCountAccessor
	{
		// Hot-path step: read the container element count, write as double.
		// Type-agnostic -- works for TArray, TSet, and TMap because all three
		// store their element count at the same binary offset (the Num field
		// in FScriptArray / FScriptSet / FScriptMap is layout-compatible).
		void ContainerCountStep(const void* Parent, void* ChildOut, const FAccessorParseResult& Parsed)
		{
			(void)Parsed;
			const int32 Num = FPropertyBuffer::GetContainerNum(Parent);
			*static_cast<double*>(ChildOut) = static_cast<double>(Num);
		}
	}

	bool FContainerCountAccessor::MatchesToken(const FString& UpperToken, FAccessorParseResult& OutParsed) const
	{
		if (UpperToken == TEXT("NUM") || UpperToken == TEXT("COUNT"))
		{
			// No parse-time state to stash. Classify + compile will fill
			// ContainerElementSize (unused for count) + ResolveOutputType
			// returns Double.
			OutParsed.SourceTypeHint = EPCGMetadataTypes::Unknown; // count gives no source-type hint
			return true;
		}
		return false;
	}

	bool FContainerCountAccessor::ResolveOutputType(EPCGMetadataTypes InType,
	                                                const FAccessorParseResult& Parsed,
	                                                EPCGMetadataTypes& OutType) const
	{
		(void)InType;
		(void)Parsed;
		OutType = EPCGMetadataTypes::Double;
		return true;
	}

	void FContainerCountAccessor::ApplyGet(EPCGMetadataTypes InType,
	                                       const void* Source,
	                                       EPCGMetadataTypes OutType,
	                                       void* OutValue,
	                                       const FAccessorParseResult& Parsed) const
	{
		(void)InType;
		(void)OutType;
		check(Source != nullptr);
		check(OutValue != nullptr);
		PCGExContainerCountAccessor::ContainerCountStep(Source, OutValue, Parsed);
	}

	FStepGetFn FContainerCountAccessor::GetStepGetFn(EPCGMetadataTypes InType) const
	{
		(void)InType;
		// One fn pointer for all container/element types -- the hot path
		// reads Num() from the container header, which is layout-compatible
		// across TArray, TSet, and TMap.
		return &PCGExContainerCountAccessor::ContainerCountStep;
	}

	ISubAccessor::ECompileAction FContainerCountAccessor::ClassifyForInType(
		EPCGMetadataTypes InType, const FAccessorParseResult& Parsed,
		const FPCGMetadataAttributeDesc* SourceDesc) const
	{
		(void)InType;
		(void)Parsed;
		// Keep for any container kind (Array, Set, Map). All three UE
		// container types store their element count at a compatible binary
		// offset, so the single hot-path function works for all of them.
		// IsSingleValue() returns true iff ContainerTypes is empty.
		if (!SourceDesc || SourceDesc->IsSingleValue())
		{
			return ECompileAction::Drop;
		}
		return ECompileAction::Keep;
	}

	FString FContainerCountAccessor::GetDisplayName() const
	{
		return TEXT("ContainerCount");
	}
}
