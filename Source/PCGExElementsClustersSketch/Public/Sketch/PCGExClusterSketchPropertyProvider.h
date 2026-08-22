// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPropertyWriter.h"

struct FPCGExClusterSketchEdge;
struct FPCGExClusterSketchVertex;
struct FPCGExSketchDataLayer;
struct FPCGExSketchDataRecord;

/**
 * Reads one authored sketch layer as an IPCGExPropertyProvider, for the print writers and for
 * decorators computing from authored values.
 *
 * SourceIndex is a MODEL item index -- a vertex index for a vertex layer, a model edge index for an
 * edge layer. Callers writing output rows translate first (edges through
 * FPCGExClusterSketchPrintContext::ParentToModelEdge); the provider never sees an output index.
 *
 * Fully resolved by the constructor and immutable afterwards, so GetPropertyAt is const, re-entrant
 * and allocation-free -- mandatory, since the edge provider is read concurrently from every subgraph's
 * pre-compile hook.
 *
 * Holds raw pointers into the model, which the print path only ever reads: build one per print, drop it
 * when the print ends.
 */
class PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExSketchLayerPropertyProvider : public IPCGExPropertyProvider
{
public:
	FPCGExSketchLayerPropertyProvider(const FPCGExSketchDataLayer& InLayer, TConstArrayView<FPCGExClusterSketchVertex> InVertices);
	FPCGExSketchLayerPropertyProvider(const FPCGExSketchDataLayer& InLayer, TConstArrayView<FPCGExClusterSketchEdge> InEdges);

	virtual ~FPCGExSketchLayerPropertyProvider() override = default;

	//~Begin IPCGExPropertyProvider
	/** Unreachable, and unimplementable: records are sparse overrides, so a per-item array would have to
	 *  be a dangling temporary. GetPropertyAt is overridden and fully bypasses this. */
	virtual TConstArrayView<FInstancedStruct> GetProperties(int32 Index) const override
	{
		return {};
	}

	/** Unreachable: nothing on the print path auto-populates outputs from a registry. */
	virtual TConstArrayView<FPCGExPropertyRegistryEntry> GetPropertyRegistry() const override
	{
		return {};
	}

	virtual const FInstancedStruct* FindPrototypeProperty(FName PropertyName) const override;

	/** NEVER null for a name the schema declares -- see FPCGExSketchDataLayer::ResolveEffectiveFrom. */
	virtual const FInstancedStruct* GetPropertyAt(int32 SourceIndex, FName PropertyName) const override;
	//~End IPCGExPropertyProvider

protected:
	const FPCGExSketchDataLayer& Layer;

	/** Model item index -> its record, null for the sparse majority. Empty when the layer holds no
	 *  record at all, which resolves everything to schema defaults just the same. */
	TArray<const FPCGExSketchDataRecord*> ItemRecords;
};
