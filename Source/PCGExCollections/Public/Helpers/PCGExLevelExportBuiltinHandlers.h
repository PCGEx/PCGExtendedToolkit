// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Helpers/PCGExLevelExportHandler.h"

#include "PCGExLevelExportBuiltinHandlers.generated.h"

/**
 * The three content kinds the default exporter has always produced, as handlers. None of them
 * claims: the Mesh / Actor / Level split stays UPCGExDefaultLevelDataExporter::ClassifyActor's
 * (overridable) decision; each handler harvests the candidates classified into its slot. Registered
 * by FPCGExCollectionsModule::StartupModule.
 */

/** Static-mesh components of Mesh-classified actors -> "Meshes" pin, shared mesh collection. */
UCLASS()
class PCGEXCOLLECTIONS_API UPCGExMeshExportHandler : public UPCGExLevelExportHandler
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FPCGExExportSlotDesc GetSlotDesc() const override;
	virtual TSharedPtr<const IPCGExExportSlotPolicy> MakePolicy() const override;
	virtual int32 GetPriority() const override { return 10; }
	virtual void Collect(const FPCGExExportCandidate& Candidate, const FPCGExLevelExportSource& Source, const UPCGExLevelDataExporter* Exporter, FPCGExExportSlotWriter& Writer) const override;
	virtual void FinalizeSlot(FPCGExExportSlotWriter& Writer, UObject* Outer, const UPCGExLevelDataExporter* Exporter) const override;
	virtual void WriteRawAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const override;
#endif
};

/** Actor-classified actors -> "Actors" pin, per-entry actor collection (class + property delta). */
UCLASS()
class PCGEXCOLLECTIONS_API UPCGExActorExportHandler : public UPCGExLevelExportHandler
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FPCGExExportSlotDesc GetSlotDesc() const override;
	virtual TSharedPtr<const IPCGExExportSlotPolicy> MakePolicy() const override;
	virtual int32 GetPriority() const override { return 20; }
	virtual void Collect(const FPCGExExportCandidate& Candidate, const FPCGExLevelExportSource& Source, const UPCGExLevelDataExporter* Exporter, FPCGExExportSlotWriter& Writer) const override;
	virtual void WriteItemAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const override;
	virtual void WriteRawAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const override;
	virtual void FinalizeEmbeddedCollection(UPCGExAssetCollection* Collection, FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const override;
#endif
};

/** Level instances -> "Levels" pin, shared level collection. */
UCLASS()
class PCGEXCOLLECTIONS_API UPCGExLevelInstanceExportHandler : public UPCGExLevelExportHandler
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FPCGExExportSlotDesc GetSlotDesc() const override;
	virtual TSharedPtr<const IPCGExExportSlotPolicy> MakePolicy() const override;
	virtual int32 GetPriority() const override { return 30; }
	virtual void Collect(const FPCGExExportCandidate& Candidate, const FPCGExLevelExportSource& Source, const UPCGExLevelDataExporter* Exporter, FPCGExExportSlotWriter& Writer) const override;
	virtual void WriteRawAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const override;
#endif
};

#if WITH_EDITOR
namespace PCGExLevelExport
{
	/** Registers the three built-in handlers. Called once from the Collections module startup. */
	PCGEXCOLLECTIONS_API void RegisterBuiltinHandlers();
	PCGEXCOLLECTIONS_API void UnregisterBuiltinHandlers();

	/** True for the three built-in handler classes (what bUseRegisteredHandlers == false keeps). */
	PCGEXCOLLECTIONS_API bool IsBuiltinHandlerClass(const UClass* HandlerClass);
}
#endif
