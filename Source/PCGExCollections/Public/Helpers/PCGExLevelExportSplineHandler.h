// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Helpers/PCGExLevelExportHandler.h"

#include "PCGExLevelExportSplineHandler.generated.h"

class AActor;
class UActorComponent;
class UPCGSplineData;
class USplineMeshComponent;

namespace PCGExLevelExport
{
	struct PCGEXCOLLECTIONS_API FSplineExtractionOptions
	{
		/** Convert spline mesh components (ConvertSplineMesh): PCG's registry has no path for them. */
		bool bConvertSplineMeshes = false;

		/** Warn about spline content that cannot be exported: landscape splines, data of another type. */
		bool bWarnSkipped = false;

		/** Stop at the first spline: a content check. */
		bool bStopAtFirst = false;
	};

	struct PCGEXCOLLECTIONS_API FExtractedSpline
	{
		/** Null for a spline the actor-level registry functions produced. */
		const UActorComponent* Component = nullptr;

		/** Transient, and possibly shared by the registry function that produced it: copy before editing. */
		const UPCGSplineData* Data = nullptr;
	};

	/**
	 * The splines PCG's Get Spline Data reads from Actor: each component through FPCGModule's GetData function registry
	 * (PCG-generated components included), spline meshes through ConvertSplineMesh when asked, then the actor-level
	 * registry functions when no component yielded one. Splines without a segment are dropped. Game thread; nothing
	 * keeps the returned data alive.
	 */
	PCGEXCOLLECTIONS_API void ExtractSplines(AActor* Actor, const FSplineExtractionOptions& Options, TArray<FExtractedSpline>& OutSplines);

	/** Whether ExtractSplines would yield anything: the spline classification's content check. */
	PCGEXCOLLECTIONS_API bool HasSplineContent(AActor* Actor, bool bConvertSplineMeshes);

	/**
	 * A spline mesh component's path as a two-point spline in the component's space, transient. The path is exact, and so
	 * is the mesh's frame at both ends, and along the whole segment when neither end rolls. Scale follows the mesh unless
	 * it smooth-steps; slice offsets and boundaries place the mesh, not the curve, and are dropped.
	 */
	PCGEXCOLLECTIONS_API UPCGSplineData* ConvertSplineMesh(const USplineMeshComponent* Component);
}

/**
 * Splines of Spline-classified actors -> "Splines" pin, one UPCGSplineData each, no collection. Each spline is moved into
 * the export frame (its control points stay local) and carries its component's name and its actor's Property Collection
 * values on @Data; the exporter adds ActorName, value tags and the authored tags as data tags.
 */
UCLASS()
class PCGEXCOLLECTIONS_API UPCGExSplineExportHandler : public UPCGExLevelExportHandler
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FPCGExExportSlotDesc GetSlotDesc() const override;
	virtual TSharedPtr<const IPCGExExportSlotPolicy> MakePolicy() const override;
	virtual int32 GetPriority() const override { return 40; }
	virtual void Collect(const FPCGExExportCandidate& Candidate, const FPCGExLevelExportSource& Source, const UPCGExLevelDataExporter* Exporter, FPCGExExportSlotWriter& Writer) const override;
	virtual void FinalizeItemData(UPCGData* Data, int32 ItemIndex, FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const override;
	virtual void GetWrittenAttributeNames(const UPCGExLevelDataExporter* Exporter, bool bRaw, TArray<FName>& OutNames) const override;
#endif
};
