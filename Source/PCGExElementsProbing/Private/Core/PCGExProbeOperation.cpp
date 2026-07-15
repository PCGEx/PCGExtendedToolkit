// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Core/PCGExProbeOperation.h"


#include "Core/PCGExProbingCandidates.h"
#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"

#if WITH_EDITOR
void FPCGExProbeConfigBase::ApplyDeprecation()
{
	SearchRadius.Update(SearchRadiusInput_DEPRECATED, SearchRadiusAttribute_DEPRECATED, SearchRadiusConstant_DEPRECATED);
}

void FPCGExProbeConfigBase::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("SearchRadiusAttribute")), FName(TEXT("SearchRadius")), FName(TEXT("Attribute")), FName(TEXT("Search Radius (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("SearchRadiusConstant")), FName(TEXT("SearchRadius")), FName(TEXT("Constant")), FName(TEXT("Search Radius")));
}
#endif

bool FPCGExProbeOperation::IsDirectProbe() const
{
	return false;
}

bool FPCGExProbeOperation::RequiresChainProcessing() const
{
	return false;
}

bool FPCGExProbeOperation::Prepare(FPCGExContext* InContext)
{
	PointIO = PrimaryDataFacade->Source;

	SearchRadius = BaseConfig->SearchRadius.GetValueSetting();
	if (!SearchRadius->Init(PrimaryDataFacade))
	{
		return false;
	}
	SearchRadiusOffset = SearchRadius->IsConstant() ? 0 : BaseConfig->SearchRadiusOffset;

	return true;
}

void FPCGExProbeOperation::ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
{
}

bool FPCGExProbeOperation::IsGlobalProbe() const
{
	return false;
}

bool FPCGExProbeOperation::WantsOctree() const
{
	return false;
}

void FPCGExProbeOperation::PrepareBestCandidate(const int32 Index, PCGExProbing::FBestCandidate& InBestCandidate, PCGExMT::FScopedContainer* Container)
{
}

void FPCGExProbeOperation::ProcessCandidateChained(const int32 Index, const int32 CandidateIndex, PCGExProbing::FCandidate& Candidate, PCGExProbing::FBestCandidate& InBestCandidate, PCGExMT::FScopedContainer* Container)
{
}

void FPCGExProbeOperation::ProcessBestCandidate(const int32 Index, PCGExProbing::FBestCandidate& InBestCandidate, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
{
}

void FPCGExProbeOperation::ProcessNode(const int32 Index, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container)
{
}

void FPCGExProbeOperation::ProcessAll(TSet<uint64>& OutEdges) const
{
}

double FPCGExProbeOperation::GetSearchRadius(const int32 Index) const
{
	return FMath::Square(SearchRadius->Read(Index) + SearchRadiusOffset);
}
