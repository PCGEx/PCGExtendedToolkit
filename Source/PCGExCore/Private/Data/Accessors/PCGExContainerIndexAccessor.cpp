// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Accessors/PCGExContainerIndexAccessor.h"

#include "Data/PCGExData.h"
#include "Data/Buffers/PCGExBufferProperty.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataCommon.h"

namespace PCGExData
{
	namespace PCGExContainerIndexAccessor
	{
		// Parse a non-negative integer from a token, with optional surrounding
		// brackets. Returns true on success and writes the index to OutIndex.
		//
		// Accepts: "0", "1", "42", "[0]", "[42]".
		// Rejects: "", "-1", "1.5", "0x1", "abc", "[", "[]", "[0", "0]".
		bool ParseContainerIndexToken(const FString& UpperToken, int32& OutIndex)
		{
			const int32 Len = UpperToken.Len();
			if (Len == 0)
			{
				return false;
			}

			int32 Begin = 0;
			int32 End = Len;

			// Bracket-wrapped form requires both ends + at least one digit between.
			if (UpperToken[0] == TEXT('['))
			{
				if (Len < 3)
				{
					return false;
				} // "[]" or "[" alone
				if (UpperToken[Len - 1] != TEXT(']'))
				{
					return false;
				}
				Begin = 1;
				End = Len - 1;
			}

			// All remaining chars must be ASCII digits. No sign, no decimal.
			for (int32 i = Begin; i < End; ++i)
			{
				const TCHAR C = UpperToken[i];
				if (C < TEXT('0') || C > TEXT('9'))
				{
					return false;
				}
			}

			// Accumulate. Guard against overflow by clamping to INT32_MAX.
			int64 Acc = 0;
			for (int32 i = Begin; i < End; ++i)
			{
				Acc = Acc * 10 + (UpperToken[i] - TEXT('0'));
				if (Acc > TNumericLimits<int32>::Max())
				{
					return false;
				}
			}

			OutIndex = static_cast<int32>(Acc);
			return true;
		}

		// Hot-path step: element write into a TArray's FScriptArray storage.
		// Uses FProperty::CopyCompleteValue when the property is available
		// (non-trivially-copyable element types), memcpy otherwise.
		void ContainerIndexSetStep(void* ParentInOut, const void* NewChild, const FAccessorParseResult& Parsed)
		{
			const int32 ElementSize = Parsed.ContainerElementSize;
			check(ElementSize > 0);

			void* Dest = FPropertyBuffer::GetMutableArrayElementAt(ParentInOut, Parsed.ContainerIndex, ElementSize);
			if (!Dest)
			{
				// Out-of-range: nothing to write to. Silent no-op rather than
				// crash -- mirrors the read path's zero-fill convention.
				return;
			}

			if (Parsed.ContainerElementProperty)
			{
				Parsed.ContainerElementProperty->CopyCompleteValue(Dest, NewChild);
			}
			else
			{
				FMemory::Memcpy(Dest, NewChild, ElementSize);
			}
		}

		// Hot-path step: element read from FScriptArray-backed storage.
		// Uses Parsed.ContainerIndex + Parsed.ContainerElementSize (stashed at
		// PostClassifyFinalize time) to stride into the array. Out-of-range
		// indices produce a zeroed output (matches FSwizzleAccessor's
		// "short-read pads with zeros" convention).
		void ContainerIndexStep(const void* Parent, void* ChildOut, const FAccessorParseResult& Parsed)
		{
			const int32 ElementSize = Parsed.ContainerElementSize;
			check(ElementSize > 0);

			const void* ElementPtr = FPropertyBuffer::GetArrayElementAt(Parent, Parsed.ContainerIndex, ElementSize);
			if (ElementPtr)
			{
				FMemory::Memcpy(ChildOut, ElementPtr, ElementSize);
			}
			else
			{
				// Out-of-range / empty array: zero-fill rather than leaving
				// the caller's buffer uninitialized. Keeps downstream chain
				// steps reading deterministic bytes.
				FMemory::Memzero(ChildOut, ElementSize);
			}
		}
	}

	bool FContainerIndexAccessor::MatchesToken(const FString& UpperToken, FAccessorParseResult& OutParsed) const
	{
		int32 Index = -1;
		if (!PCGExContainerIndexAccessor::ParseContainerIndexToken(UpperToken, Index))
		{
			return false;
		}

		OutParsed.ContainerIndex = Index;
		// No source-type hint: a numeric index gives us nothing about the
		// source attribute's type (the wrapping container is what matters,
		// and that's learned from the Desc at compile time).
		OutParsed.SourceTypeHint = EPCGMetadataTypes::Unknown;
		return true;
	}

	bool FContainerIndexAccessor::ResolveOutputType(EPCGMetadataTypes InType,
	                                                const FAccessorParseResult& Parsed,
	                                                EPCGMetadataTypes& OutType) const
	{
		(void)Parsed;
		// Output type = InType. For PCG metadata attributes, a TArray<T>
		// attribute reports its type as T (the element type) with
		// container-ness in the Desc; so "the type after an index step"
		// is the same InType we were given.
		OutType = InType;
		return InType != EPCGMetadataTypes::Unknown;
	}

	void FContainerIndexAccessor::ApplyGet(EPCGMetadataTypes InType,
	                                       const void* Source,
	                                       EPCGMetadataTypes OutType,
	                                       void* OutValue,
	                                       const FAccessorParseResult& Parsed) const
	{
		(void)InType;
		(void)OutType;
		check(Source != nullptr);
		check(OutValue != nullptr);
		PCGExContainerIndexAccessor::ContainerIndexStep(Source, OutValue, Parsed);
	}

	void FContainerIndexAccessor::ApplySet(EPCGMetadataTypes InType,
	                                       void* TargetInOut,
	                                       EPCGMetadataTypes SourceType,
	                                       const void* Source,
	                                       const FAccessorParseResult& Parsed) const
	{
		(void)InType;
		(void)SourceType;
		check(TargetInOut != nullptr);
		check(Source != nullptr);
		PCGExContainerIndexAccessor::ContainerIndexSetStep(TargetInOut, Source, Parsed);
	}

	FStepGetFn FContainerIndexAccessor::GetStepGetFn(EPCGMetadataTypes InType) const
	{
		(void)InType;
		// One fn pointer for all element types -- sizeof(element) is carried
		// in Parsed.ContainerElementSize, populated by PostClassifyFinalize.
		return &PCGExContainerIndexAccessor::ContainerIndexStep;
	}

	FStepSetFn FContainerIndexAccessor::GetStepSetFn(EPCGMetadataTypes InType) const
	{
		(void)InType;
		return &PCGExContainerIndexAccessor::ContainerIndexSetStep;
	}

	ISubAccessor::ECompileAction FContainerIndexAccessor::ClassifyForInType(
		EPCGMetadataTypes InType, const FAccessorParseResult& Parsed,
		const FPCGMetadataAttributeDesc* SourceDesc) const
	{
		(void)InType;
		(void)Parsed;
		// Keep iff the source is an Array-backed container. Set/Map aren't
		// positionally-indexed meaningfully -- Drop for 5b.
		if (!SourceDesc || !SourceDesc->IsArray())
		{
			return ECompileAction::Drop;
		}
		return ECompileAction::Keep;
	}

	void FContainerIndexAccessor::PostClassifyFinalize(EPCGMetadataTypes InType,
	                                                   FAccessorParseResult& InOutParsed,
	                                                   const FPCGMetadataAttributeDesc* SourceDesc) const
	{
		(void)InType;
		// Classify has already verified SourceDesc + Array container.
		// Compute and stash the inner element size so the hot path can
		// stride into FScriptArray storage without carrying Desc forward.
		check(SourceDesc);
		InOutParsed.ContainerElementSize = FPropertyBuffer::GetInnerElementSizeFromDesc(*SourceDesc);
		check(InOutParsed.ContainerElementSize > 0);
	}

	FString FContainerIndexAccessor::GetDisplayName() const
	{
		return TEXT("ContainerIndex");
	}
}
