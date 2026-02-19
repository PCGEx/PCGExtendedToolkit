// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"

#include "PCGExConnectorPatternGraph.generated.h"

class UPCGExConnectorPatternAsset;
class UPCGExValencyConnectorSet;

/**
 * UEdGraph subclass for the Connector Pattern visual editor.
 * Each disconnected subgraph represents one pattern.
 */
UCLASS()
class PCGEXELEMENTSVALENCYEDITOR_API UPCGExConnectorPatternGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	/** The asset this graph edits */
	TWeakObjectPtr<UPCGExConnectorPatternAsset> OwningAsset;

	/** Compile graph topology into the owning asset's Patterns[] array, then call Asset->Compile() */
	void CompileGraphToAsset();

	/** Build graph nodes and wires from the owning asset's Patterns[] array (for legacy/first-open) */
	void BuildGraphFromAsset();

	/** Rebuild all node pins from current ConnectorSet (call when ConnectorSet changes) */
	void RebuildAllNodePins();

	/** Flush PCG cache and regenerate all PCG components in editor worlds */
	void RefreshPCGComponents();

	/** Get the ConnectorSet from the owning asset (may be null) */
	UPCGExValencyConnectorSet* GetConnectorSet() const;
};
