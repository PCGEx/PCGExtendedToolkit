// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSpawnDynamicMesh.h"

#include "PCGComponent.h"
#include "PCGExTopology.h"
#include "PCGPin.h"
#include "Components/PCGExDynamicMeshComponent.h"
#include "Data/PCGDynamicMeshData.h"
#include "Elements/PCGExSpawnDynamicMeshReuse.h"

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

FPCGElementPtr UPCGExSpawnDynamicMeshSettings::CreateElement() const { return MakeShared<FPCGExSpawnDynamicMeshElement>(); }

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

		// Pick up the component a previous generation built for this exact input instead of creating another one.
		// Every new component is named with MakeUniqueObjectName, and the name of the one it replaces is still taken
		// until the next garbage collection, so recreating them rewrites the target actor's package on every bake.
		const FPCGCrc InputCrc = PCGExSpawnDynamicMesh::ComputeInputCrc(this, InContext, Settings, Input);
		if (PCGExSpawnDynamicMesh::TryReuseComponent(SourcePCGComponent, TargetActor, InputCrc) != nullptr)
		{
			InContext->OutputData.TaggedData.Emplace(Input);
			Context->AddNotifyActor(TargetActor);
			continue;
		}

		const FString ComponentName = TEXT("PCGDynamicMeshComponent");
		const EObjectFlags ObjectFlags = (bIsPreviewMode ? RF_Transient : RF_NoFlags);
		UPCGExDynamicMeshComponent* DynamicMeshComponent = NewObject<UPCGExDynamicMeshComponent>(TargetActor, MakeUniqueObjectName(TargetActor, UPCGExDynamicMeshComponent::StaticClass(), FName(ComponentName)), ObjectFlags);

		if (!DynamicMeshComponent) { continue; }

		SourcePCGComponent->IgnoreChangeOriginDuringGenerationWithScope(DynamicMeshComponent, [&]()
		{
			const UDynamicMesh* DynamicMesh = DynMeshData->GetDynamicMesh();
			const TArray<TObjectPtr<UMaterialInterface>>& Materials = DynMeshData->GetMaterials();

			//DynMeshData->InitializeDynamicMeshComponentFromData(Component);

			for (int32 i = 0; i < Materials.Num(); ++i) { DynamicMeshComponent->SetMaterial(i, Materials[i]); }
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

		for (const FString& Tag : Input.Tags) { DynamicMeshComponent->ComponentTags.AddUnique(*Tag); }

		UPCGManagedComponent* ManagedComponent = Context->AttachManagedComponent(TargetActor, DynamicMeshComponent, Settings->AttachmentRules.GetRules(), PCGExSpawnDynamicMesh::GetManagedComponentClass());

		// Stamp the resource with the Crc of the input it was built from, so the next generation recognizes it.
		PCGExSpawnDynamicMesh::EnableReuse(ManagedComponent, InputCrc);

		InContext->OutputData.TaggedData.Emplace(Input);
		Context->AddNotifyActor(TargetActor);
	}

	Context->ExecuteOnNotifyActors(Settings->PostProcessFunctionNames);

	return Context->TryComplete(true);
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
