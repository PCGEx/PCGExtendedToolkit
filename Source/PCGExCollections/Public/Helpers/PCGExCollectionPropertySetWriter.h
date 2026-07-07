// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPropertyWriter.h"
#include "StructUtils/InstancedStruct.h"

class UPCGExAssetCollection;
struct FPCGExAssetCollectionEntry;
struct FPCGExContext;
class FPCGMetadataAttributeBase;
class UPCGMetadata;
class UPCGData;

namespace PCGExCollections
{
	/**
	 * Scan a set of collections for a property prototype by name. First match wins; returns
	 * nullptr if no collection declares the property. Used when building writers that will
	 * later be fed values from heterogeneous hosts.
	 */
	PCGEXCOLLECTIONS_API const FInstancedStruct* FindPrototypeProperty(
		FName PropertyName,
		TConstArrayView<const UPCGExAssetCollection*> Collections);

	/**
	 * Resolve the effective source property for a given entry: per-entry override first,
	 * then the host collection's default. Returns nullptr if neither is present/valid.
	 */
	PCGEXCOLLECTIONS_API const FInstancedStruct* ResolveEntrySourceProperty(
		const FPCGExAssetCollectionEntry* Entry,
		const UPCGExAssetCollection* Host,
		FName PropertyName);

	/**
	 * @Data-domain counterpart to FPCGExCollectionPropertySetWriter: writes each configured
	 * schema property as a single @Data value on InData, sourced from Host->CollectionProperties.
	 * Entry overrides are NOT consulted (schema only) -- this is the annotation path, not the
	 * per-entry write.
	 *
	 * Null Host -> per-property writes are skipped (no attribute is declared). Use the identity
	 * attributes (RootCollectionIndex etc.) to detect null-resolved inputs downstream.
	 *
	 * Standalone helper rather than a method on FPCGExCollectionPropertySetWriter so callers can
	 * use it without first calling Initialize -- @Data writes don't share machinery with the
	 * per-row @Element write loop and don't need the prototype-cached Writers list.
	 */
	PCGEXCOLLECTIONS_API void WriteSchemaToDataDomain(
		FPCGExContext* InContext,
		const FPCGExPropertyOutputSettings& OutputSettings,
		const UPCGExAssetCollection* Host,
		UPCGData* InData);

	/**
	 * @Element-domain sibling: writes each configured schema property as a per-row attribute on
	 * InData, where row r reads from PerRowHosts[r]->CollectionProperties.
	 *
	 * Heterogeneous-host friendly: rows can reference different collections; the prototype (and
	 * thus the attribute type + null-host default value) is taken from the first collection in
	 * PerRowHosts that declares the property. Rows whose host doesn't declare the property fall
	 * back to that same prototype default.
	 *
	 * All-null PerRowHosts -> skips the property entirely (no prototype available, no schema to
	 * read). Empty PerRowHosts -> no-op.
	 *
	 * Uses the same accessor-keys pattern as the identity-attr writes in AnnotateSources, so it
	 * works uniformly for point data (keys = point indices) and attribute-set data (keys = entry
	 * indices).
	 */
	PCGEXCOLLECTIONS_API void WriteSchemaToElementDomain(
		FPCGExContext* InContext,
		const FPCGExPropertyOutputSettings& OutputSettings,
		UPCGData* InData,
		TConstArrayView<const UPCGExAssetCollection*> PerRowHosts);

	/**
	 * Writes FPCGExPropertyOutputSettings-driven custom properties to an attribute set
	 * (UPCGMetadata) for collection-backed generation nodes.
	 *
	 * Lifecycle (single-threaded init, per-entry writes):
	 *   Writer.Initialize(Context, OutputSettings, RootCollection, FallbackHosts, Metadata);
	 *   for (... entries ...)
	 *   {
	 *       const int64 Key = Metadata->AddEntry();
	 *       // Write your node-specific attributes here
	 *       Writer.WriteEntry(Key, Entry, Host);
	 *   }
	 *
	 * Prototype resolution: scans RootCollection->CollectionProperties first, then each
	 * FallbackHost's CollectionProperties if not found. Missing properties log a warning
	 * and are skipped. Attributes are created on the provided Metadata.
	 *
	 * Per-entry source resolution: Entry->PropertyOverrides takes precedence, then
	 * Host->CollectionProperties default. Type mismatches between source and writer are
	 * skipped silently (leaves default value on the attribute).
	 *
	 * Thin wrapper over FPCGExPropertySetWriter -- this class is now collection-only glue
	 * (turn UPCGExAssetCollection* / FPCGExAssetCollectionEntry* into the lookup callables the
	 * generic writer expects). Anything that wants the same metadata-write machinery on a
	 * non-collection schema source should use FPCGExPropertySetWriter directly.
	 */
	class PCGEXCOLLECTIONS_API FPCGExCollectionPropertySetWriter
	{
	public:
		FPCGExCollectionPropertySetWriter() = default;

		/**
		 * Scan output configs, find prototypes, clone writer instances and create attributes.
		 * Safe to call with no outputs configured - HasOutputs() will simply return false.
		 *
		 * @param InContext         Context for warning logs.
		 * @param OutputSettings    The user-facing output config.
		 * @param RootCollection    Primary collection to search for property prototypes.
		 * @param FallbackHosts     Additional hosts to scan when a property is missing from RootCollection.
		 * @param Metadata          Metadata owning the attribute set being written.
		 * @return true if at least one writer was successfully initialized.
		 */
		bool Initialize(
			FPCGExContext* InContext,
			const FPCGExPropertyOutputSettings& OutputSettings,
			const UPCGExAssetCollection* RootCollection,
			TConstArrayView<const UPCGExAssetCollection*> FallbackHosts,
			UPCGMetadata* Metadata);

		/**
		 * Write all configured property values for the given metadata key.
		 * Resolves per-entry override then falls back to Host default.
		 */
		void WriteEntry(int64 Key, const FPCGExAssetCollectionEntry* Entry, const UPCGExAssetCollection* Host);

		bool HasOutputs() const
		{
			return Inner.HasOutputs();
		}

	protected:
		/** Lightweight IPCGExPropertyProvider used purely for prototype lookup during Initialize.
		 *  Per-entry source resolution is handled by WriteEntry directly (via WriteAt + the existing
		 *  ResolveEntrySourceProperty helper) -- the dynamic (Entry, Host) shape doesn't fit the
		 *  per-source-index lookup that provider-based WriteEntry expects. */
		struct FCollectionPrototypeProvider final : IPCGExPropertyProvider
		{
			TArray<const UPCGExAssetCollection*> SearchOrder;

			virtual TConstArrayView<FInstancedStruct> GetProperties(int32 /*Index*/) const override
			{
				return {};
			}

			virtual TConstArrayView<FPCGExPropertyRegistryEntry> GetPropertyRegistry() const override
			{
				return {};
			}

			virtual const FInstancedStruct* FindPrototypeProperty(FName PropertyName) const override;
		};

		FCollectionPrototypeProvider Provider;
		FPCGExPropertySetWriter Inner;
	};
}
