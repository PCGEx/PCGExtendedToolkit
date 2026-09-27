// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExDataFilterDetails.h"
#include "PCGExDataForwardDetails.h"
#include "Types/PCGExAttributeIdentity.h"

class UPCGBasePointData;

namespace PCGExMT
{
	struct FScope;
}

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
		// InIgnoredAttributes: source attribute names never forwarded, applied before the details name filter.
		FDataForwardHandler(const FPCGExForwardDetails& InDetails, const TSharedPtr<FFacade>& InSourceDataFacade, const TSharedPtr<FFacade>& InTargetDataFacade, const EForwardDomain InDomain = EForwardDomain::Inherit, const TSet<FName>* InIgnoredAttributes = nullptr);

		// Metadata-sourced handler: identities come from InSourceMetadata (e.g. the scratch metadata an engine
		// ProjectPoint/SamplePoint writes into), Elements domain only, writers pre-created on the target facade.
		// Only ForwardEntry is valid on such a handler; the facade-sourced Forward overloads have no source rows.
		FDataForwardHandler(const FPCGExForwardDetails& InDetails, const UPCGMetadata* InSourceMetadata, const TSharedPtr<FFacade>& InTargetDataFacade, const TSet<FName>* InIgnoredAttributes = nullptr);

		void ValidateIdentities(FValidateFn&& Fn);

		bool IsEmpty() const
		{
			return Identities.IsEmpty();
		}

		void Forward(const int32 SourceIndex, const int32 TargetIndex);

		// Metadata-sourced variant: copies the source attributes' values at SourceKey onto target row TargetIndex.
		// Safe from concurrent tasks once built; a PCGInvalidEntryKey source is a no-op.
		void ForwardEntry(const PCGMetadataEntryKey SourceKey, const int32 TargetIndex);

		// Bulk ForwardEntry: SourceKeys is indexed by target row, invalid keys are skipped. One key/value resolution per
		// attribute instead of per row; call once after the parallel loop, before the facade write.
		void ForwardEntries(TConstArrayView<PCGMetadataEntryKey> SourceKeys);

		// Scoped ForwardEntry: target t in Scope receives the value at SourceKeyPerTarget[t]; PCGInvalidEntryKey leaves
		// that target untouched. One bulk key/value resolution per attribute per scope, raw arrays on the Elements domain.
		// Touches no handler state, so concurrent scopes may call it on the same handler.
		void ForwardEntriesScoped(const PCGExMT::FScope& Scope, TConstArrayView<PCGMetadataEntryKey> SourceKeyPerTarget) const;

		// Prepared-target variant (requires the target-facade constructor): fans one source row out to many
		// target indices through the pre-created writers -- no lazy buffer creation, safe from concurrent tasks.
		void Forward(const int32 SourceIndex, const TArray<int32>& Indices);

		// Prepared-target variant: target i in Scope receives source row SourceIndexPerTarget[i]; a negative row leaves
		// that target untouched. One type dispatch per attribute per scope, raw arrays on the Elements domain.
		void ForwardScoped(const PCGExMT::FScope& Scope, TConstArrayView<int32> SourceIndexPerTarget);

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
