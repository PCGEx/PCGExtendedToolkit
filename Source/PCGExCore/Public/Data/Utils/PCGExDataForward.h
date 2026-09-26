// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataFilterDetails.h"
#include "PCGExDataForwardDetails.h"
#include "Types/PCGExAttributeIdentity.h"

class UPCGBasePointData;

namespace PCGExData
{
	struct FConstPoint;
	class IBuffer;
	class FDataForwardHandler;
	class IAttributeBroadcaster;
}

namespace PCGExData
{
	class PCGEXCORE_API FDataForwardHandler
	{
		FPCGExForwardDetails Details;
		TSharedPtr<FFacade> SourceDataFacade;
		TSharedPtr<FFacade> TargetDataFacade;
		TArray<FAttributeIdentity> Identities;
		TArray<TSharedPtr<IBuffer>> Readers;
		TArray<TSharedPtr<IBuffer>> Writers;
		EForwardDomain Domain = EForwardDomain::Inherit;

		/** Identifier the source attribute is written under on the target, per Domain. */
		FPCGAttributeIdentifier GetTargetIdentifier(const FAttributeIdentity& Identity) const;

		/** True when Domain moves this attribute to a different domain than its source one. */
		bool RedirectsDomain(const FAttributeIdentity& Identity) const;

	public:
		using FValidateFn = std::function<bool(const FAttributeIdentity&)>;

		~FDataForwardHandler() = default;
		FDataForwardHandler(const FPCGExForwardDetails& InDetails, const TSharedPtr<FFacade>& InSourceDataFacade, const EForwardDomain InDomain = EForwardDomain::Inherit);
		FDataForwardHandler(const FPCGExForwardDetails& InDetails, const TSharedPtr<FFacade>& InSourceDataFacade, const TSharedPtr<FFacade>& InTargetDataFacade, const EForwardDomain InDomain = EForwardDomain::Inherit);

		void ValidateIdentities(FValidateFn&& Fn);

		bool IsEmpty() const
		{
			return Identities.IsEmpty();
		}

		void Forward(const int32 SourceIndex, const int32 TargetIndex);
		void Forward(const int32 SourceIndex, const TSharedPtr<FFacade>& InTargetDataFacade);
		void Forward(const int32 SourceIndex, const TSharedPtr<FFacade>& InTargetDataFacade, const TArray<int32>& Indices);
		void Forward(const int32 SourceIndex, UPCGMetadata* InTargetMetadata);

		// Copy k owns Target points [k*Stride, (k+1)*Stride) and gets source row SourceIndices[k] per element, one value key
		// per copy; ignores Domain and replaces same-named Elements attributes. Target's entries must already be its own.
		void ForwardToCopies(TConstArrayView<int32> SourceIndices, UPCGBasePointData* Target, int32 Stride) const;

		// Forwards source attributes onto a single entry (per-row, element domain) of any target metadata -- e.g. a source path's @Data onto one attribute-set row.
		void Forward(const int32 SourceIndex, UPCGMetadata* InTargetMetadata, const int64 TargetKey);
	};
}
