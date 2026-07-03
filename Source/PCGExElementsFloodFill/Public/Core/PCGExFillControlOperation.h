// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFloodFill.h"
#include "Factories/PCGExOperation.h"
#include "UObject/Object.h"

/**
 *
 */
class PCGEXELEMENTSFLOODFILL_API FPCGExFillControlOperation : public FPCGExOperation
{
public:
	const UPCGExFillControlsFactoryData* Factory = nullptr;

	TSharedPtr<TArray<int32>> SettingsIndex;

	virtual bool PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler);

	// Scoring capability - controls can contribute to candidate scores
	virtual bool DoesScoring() const;
	virtual void ScoreCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, PCGExFloodFill::FCandidate& OutCandidate);

	// Validation capabilities
	virtual bool ChecksCapture() const;
	virtual bool IsValidCapture(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate);

	virtual bool ChecksProbe() const;
	virtual bool IsValidProbe(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate);

	virtual bool ChecksCandidate() const;
	virtual bool IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate);

	// Capture notification - opt in via WantsCaptureNotify() to react to a committed capture
	// (e.g. per-node child counts). OnCaptured fires once per captured node, on its diffusion's thread.
	virtual bool WantsCaptureNotify() const;
	virtual void OnCaptured(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& Candidate);

	// Probe fan-out limiting - opt in via LimitsProbeFanout() to cap how many children a node
	// spreads to. The probe keeps the best 'limit' neighbors and leaves the rest for other nodes.
	virtual bool LimitsProbeFanout() const;
	virtual int32 GetProbeFanoutLimit(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From);

	// Whether this control reads FDiffusion::TravelStack. When no control (and no node feature)
	// needs it, diffusions skip breadcrumb allocation and per-capture writes entirely.
	virtual bool WantsTravelStack() const;

	int32 GetSettingsIndex(const PCGExFloodFill::FDiffusion* Diffusion) const;

protected:
	TSharedPtr<PCGExData::FFacade> GetSourceFacade() const;

	TSharedPtr<PCGExClusters::FCluster> Cluster;
	TSharedPtr<PCGExFloodFill::FFillControlsHandler> Handler;
};
