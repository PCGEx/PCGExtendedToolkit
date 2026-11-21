// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSpawnDynamicMesh.h"

#include "PCGComponent.h"
#include "PCGCrc.h"
#include "PCGExTopology.h"
#include "PCGPin.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Components/DynamicMeshComponent.h"
#include "Core/PCGExManagedComponent.h"
#include "Core/PCGExMT.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGExDataTags.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Types/PCGExTypeOpsString.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"
#define PCGEX_NAMESPACE SpawnDynamicMesh

#pragma region UPCGSettings interface

TArray<FPCGPinProperties> UPCGExSpawnDynamicMeshSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_MESH(PCGExTopology::Labels::SourceMeshLabel, "PCG Dynamic Mesh", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSpawnDynamicMeshSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_MESH(PCGExTopology::Labels::SourceMeshLabel, "PCG Dynamic Mesh", Normal)
	return PinProperties;
}

FPCGElementPtr UPCGExSpawnDynamicMeshSettings::CreateElement() const
{
	return MakeShared<FPCGExSpawnDynamicMeshElement>();
}

#pragma endregion

bool FPCGExSpawnDynamicMeshElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(SpawnDynamicMesh)

	AActor* TargetActor = Settings->TargetActor.IsValid() ? Settings->TargetActor.Get() : InContext->GetTargetActor(nullptr);
	if (!TargetActor)
	{
		PCGLog::LogErrorOnGraph(LOCTEXT("InvalidTargetActor", "Invalid target actor."), InContext);
		return true;
	}

	UPCGComponent* SourcePCGComponent = Context->GetMutableComponent();
	const bool bIsPreviewMode = SourcePCGComponent->IsInPreviewMode();

	PCGEX_MAKE_SHARED(TemplateResources, TSet<FSoftObjectPath>)
	Settings->TemplateDescriptor.GetAssetPaths(*TemplateResources.Get());
	if (!TemplateResources->IsEmpty())
	{
		PCGExHelpers::LoadBlockingTracked_AnyThread(TemplateResources, Context);
	}

	// Output creates UObjects (NewObject) and attaches components -- illegal during a package save / GC. Defer (re-tick) until clear.
	PCGEX_DEFER_IF_OBJECT_WORK_BLOCKED

	const FPCGCrc ExecutionCrc = Context->GetExecutionCrc();
	const FString ClusterIdTagPrefix = PCGExClusters::Labels::TagStr_PCGExCluster + PCGExData::TagSeparator;

	int32 Index = -1;
	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PCGExTopology::Labels::SourceMeshLabel))
	{
		Index++;

		const UPCGDynamicMeshData* DynMeshData = Cast<UPCGDynamicMeshData>(Input.Data);
		if (!DynMeshData)
		{
			PCGLog::InputOutput::LogInvalidInputDataError(InContext);
			continue;
		}

		// One slot per input: a regeneration rebuilds the component it already has there, keeping its name.
		FPCGCrc SlotCrc = ExecutionCrc;
		SlotCrc.Combine(static_cast<uint32>(Index));

		UPCGExManagedComponent* Reclaimed = Context->ReclaimManagedComponent(TargetActor, UDynamicMeshComponent::StaticClass(), SlotCrc);
		UDynamicMeshComponent* DynamicMeshComponent = nullptr;

		if (Reclaimed)
		{
			DynamicMeshComponent = CastChecked<UDynamicMeshComponent>(Reclaimed->GeneratedComponent.Get());
			DynamicMeshComponent->EmptyOverrideMaterials();
		}
		else
		{
			const FString ComponentName = TEXT("PCGDynamicMeshComponent");
			const EObjectFlags ObjectFlags = (bIsPreviewMode ? RF_Transient : RF_NoFlags);
			DynamicMeshComponent = NewObject<UDynamicMeshComponent>(TargetActor, MakeUniqueObjectName(TargetActor, UDynamicMeshComponent::StaticClass(), FName(ComponentName)), ObjectFlags);
		}

		if (!DynamicMeshComponent)
		{
			continue;
		}

		SourcePCGComponent->IgnoreChangeOriginDuringGenerationWithScope(DynamicMeshComponent, [&]()
		{
			const UDynamicMesh* DynamicMesh = DynMeshData->GetDynamicMesh();
			const TArray<TObjectPtr<UMaterialInterface>>& Materials = DynMeshData->GetMaterials();

			for (int32 i = 0; i < Materials.Num(); ++i)
			{
				DynamicMeshComponent->SetMaterial(i, Materials[i]);
			}
			Settings->TemplateDescriptor.InitComponent(DynamicMeshComponent);
			DynamicMeshComponent->SetMesh(FDynamicMesh3(DynamicMesh->GetMeshRef()));
		});

		if (!Settings->PropertyOverrideDescriptions.IsEmpty())
		{
			FPCGObjectOverrides DescriptorOverride(DynamicMeshComponent);
			DescriptorOverride.Initialize(Settings->PropertyOverrideDescriptions, DynamicMeshComponent, DynMeshData, InContext);
			if (DescriptorOverride.IsValid() && !DescriptorOverride.Apply(0))
			{
				PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("FailOverride", "Failed to override descriptor for input {0}"), Index));
			}
		}

		for (const FString& Tag : Input.Tags)
		{
			// The cluster id is a per-session object id: on a saved component it would differ on every run.
			if (Tag.StartsWith(ClusterIdTagPrefix))
			{
				continue;
			}

			const FName TagName = PCGExTypeOps::MakeNameChecked(Tag);
			if (!TagName.IsNone())
			{
				DynamicMeshComponent->ComponentTags.AddUnique(TagName);
			}
		}

		Context->AttachManagedComponent(TargetActor, DynamicMeshComponent, Settings->AttachmentRules.GetRules(), SlotCrc, Reclaimed);
		Context->StageOutput(const_cast<UPCGData*>(Input.Data.Get()), PCGExTopology::Labels::SourceMeshLabel, PCGExData::EStaging::None, Input.Tags);
		Context->AddNotifyActor(TargetActor);
	}

	Context->ExecuteOnNotifyActors(Settings->PostProcessFunctionNames);

	return Context->TryComplete(true);
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
