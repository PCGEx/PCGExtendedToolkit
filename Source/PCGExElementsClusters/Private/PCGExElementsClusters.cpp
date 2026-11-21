// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsClusters.h"


#if WITH_EDITOR
#include "Core/PCGExClusterStates.h"
#include "Data/Registry/PCGDataTypeRegistry.h"
#include "Elements/Meta/NeighborSamplers/PCGExNeighborSampleFactoryProvider.h"
#include "Elements/Meta/VtxProperties/PCGExVtxPropertyFactoryProvider.h"
#include "Styling/AppStyle.h"
#endif

#define LOCTEXT_NAMESPACE "FPCGExElementsClustersModule"

#if WITH_EDITOR
void FPCGExElementsClustersModule::RegisterToEditor(const TSharedPtr<FSlateStyleSet>& InStyle)
{
	IPCGExModuleInterface::RegisterToEditor(InStyle);

	PCGEX_REGISTER_PIN_ICON(IN_Vtx)
	PCGEX_REGISTER_PIN_ICON(OUT_Vtx)

	PCGEX_REGISTER_PIN_ICON(IN_Edges)
	PCGEX_REGISTER_PIN_ICON(OUT_Edges)

	PCGEX_START_PCG_REGISTRATION
	PCGEX_REGISTER_DATA_TYPE(ClusterState, ClusterState)
	PCGEX_REGISTER_DATA_TYPE(NeighborSampler, NeighborSampler)
	PCGEX_REGISTER_DATA_TYPE(VtxProperty, VtxProperty)
}
#endif

#undef LOCTEXT_NAMESPACE

PCGEX_IMPLEMENT_MODULE(FPCGExElementsClustersModule, PCGExElementsClusters)
