// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExLevelExportSplineHandler.h"

#include "PCGExLog.h"
#include "PCGModule.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Data/PCGLandscapeSplineData.h"
#include "Data/PCGSplineData.h"
#include "Data/Registry/PCGGetDataFunctionRegistry.h"
#include "GameFramework/Actor.h"
#include "Grid/PCGPartitionActor.h"

#if WITH_EDITOR
#include "PCGExProperty.h"
#include "PCGExPropertyCollectionComponent.h"
#include "PCGExPropertyWriter.h"
#include "Data/PCGExDataHelpers.h"
#include "Helpers/PCGExDefaultLevelDataExporter.h"
#include "Helpers/PCGExLevelDataExporter.h"
#include "Helpers/PCGExLevelExportShared.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Paths/PCGExPathsHelpers.h"
#include "StructUtils/InstancedStruct.h"
#endif

#pragma region Extraction

namespace PCGExLevelExportSplineHandler
{
	/** Keeps the spline data of a registry output; landscape splines, which cannot be kept, are warned about when asked. */
	void KeepSplines(const FPCGDataCollection& Collection, const UActorComponent* Component, const AActor* Actor, const bool bWarnSkipped, TArray<PCGExLevelExport::FExtractedSpline>& OutSplines)
	{
		for (const FPCGTaggedData& Tagged : Collection.TaggedData)
		{
			const UPCGData* Data = Tagged.Data.Get();
			if (const UPCGSplineData* Spline = Cast<UPCGSplineData>(Data))
			{
				if (Spline->GetNumSegments() > 0)
				{
					OutSplines.Add(PCGExLevelExport::FExtractedSpline{Component, Spline});
				}
			}
			else if (bWarnSkipped && Cast<UPCGLandscapeSplineData>(Data))
			{
				UE_LOG(LogPCGEx, Warning, TEXT("Level export: landscape splines '%s' of '%s' are skipped: their spline data reads the live landscape and cannot be stored."),
				       *GetNameSafe(Component), *Actor->GetActorNameOrLabel());
			}
		}
	}
}

namespace PCGExLevelExport
{
	void ExtractSplines(AActor* Actor, const FSplineExtractionOptions& Options, TArray<FExtractedSpline>& OutSplines)
	{
		if (!Actor)
		{
			return;
		}

		const FPCGGetDataFunctionRegistry& Registry = FPCGModule::ConstGetDataFunctionRegistry();

		// Generated components are exported, as generated meshes are. Tags come from the exporter, filtered.
		FPCGGetDataFunctionRegistryParams Params;
		Params.DataTypeFilter = EPCGDataType::Spline;
		Params.bIgnorePCGGeneratedComponents = false;
		Params.bAddActorTags = false;

		const int32 NumBefore = OutSplines.Num();

		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			FPCGGetDataFunctionRegistryOutput Output;
			Registry.GetDataFromComponent(/*Context=*/nullptr, Params, Component, Output);

			const int32 NumKept = OutSplines.Num();
			PCGExLevelExportSplineHandler::KeepSplines(Output.Collection, Component, Actor, Options.bWarnSkipped, OutSplines);

			if (Options.bConvertSplineMeshes && OutSplines.Num() == NumKept)
			{
				if (const USplineMeshComponent* SplineMesh = Cast<USplineMeshComponent>(Component))
				{
					OutSplines.Add(FExtractedSpline{Component, ConvertSplineMesh(SplineMesh)});
				}
			}

			if (Options.bStopAtFirst && OutSplines.Num() > NumBefore)
			{
				return;
			}
		}

		// Actor-level registry functions, for spline systems that live on the actor. Their default path walks the
		// components again, already reported above; a PCG partition actor's needs an execution source.
		if (OutSplines.Num() == NumBefore && !Actor->IsA<APCGPartitionActor>())
		{
			FPCGGetDataFunctionRegistryOutput Output;
			Registry.GetDataFromActor(/*Context=*/nullptr, Params, Actor, Output);
			PCGExLevelExportSplineHandler::KeepSplines(Output.Collection, nullptr, Actor, /*bWarnSkipped=*/false, OutSplines);
		}
	}

	bool HasSplineContent(AActor* Actor, const bool bConvertSplineMeshes)
	{
		FSplineExtractionOptions Options;
		Options.bConvertSplineMeshes = bConvertSplineMeshes;
		Options.bStopAtFirst = true;

		TArray<FExtractedSpline> Splines;
		ExtractSplines(Actor, Options, Splines);
		return !Splines.IsEmpty();
	}

	UPCGSplineData* ConvertSplineMesh(const USplineMeshComponent* Component)
	{
		if (!Component)
		{
			return nullptr;
		}

		// Clamped as the mesh's own slice math clamps it (FSplineMeshSceneProxyDesc); only its direction matters.
		FVector Up = ClampVector(Component->GetSplineUpDir(), -FVector::OneVector, FVector::OneVector).GetSafeNormal();
		if (Up.IsNearlyZero())
		{
			Up = FVector::ZAxisVector;
		}
		const FQuat UpFromZ = FQuat::FindBetweenNormals(FVector::ZAxisVector, Up);

		// PCG's frame at a key is MakeFromXZ(tangent, Rotation * +Z); the mesh's is the same around its up rolled about
		// the tangent, in the opposite sense to FQuat's: rolling the up by -Roll matches the two at that end.
		auto EndRotation = [&UpFromZ](const FVector& Tangent, const float Roll) -> FRotator
		{
			const FVector Direction = Tangent.GetSafeNormal();
			const FQuat RollQuat = Direction.IsNearlyZero() ? FQuat::Identity : FQuat(Direction, -static_cast<double>(Roll));
			return (RollQuat * UpFromZ).Rotator();
		};

		// The mesh's 2D scale spans the frame's Y and Z whatever its forward axis.
		const FVector StartTangent = Component->GetStartTangent();
		const FVector EndTangent = Component->GetEndTangent();
		const FVector2D StartScale = Component->GetStartScale();
		const FVector2D EndScale = Component->GetEndScale();

		// Custom tangents over keys 0..1 evaluate the mesh's own Hermite segment.
		TArray<FSplinePoint> Points;
		Points.Reserve(2);
		Points.Emplace(0.f, Component->GetStartPosition(), StartTangent, StartTangent, EndRotation(StartTangent, Component->GetStartRoll()), FVector(1, StartScale.X, StartScale.Y));
		Points.Emplace(1.f, Component->GetEndPosition(), EndTangent, EndTangent, EndRotation(EndTangent, Component->GetEndRoll()), FVector(1, EndScale.X, EndScale.Y));

		UPCGSplineData* SplineData = NewObject<UPCGSplineData>();
		SplineData->Initialize(Points, /*bInClosedLoop=*/false, Component->GetComponentTransform());
		return SplineData;
	}
}

#pragma endregion

#if WITH_EDITOR

#pragma region UPCGExSplineExportHandler

namespace PCGExLevelExportSplineHandler
{
	const FName ComponentNameAttribute = TEXT("ComponentName");

	struct FScratch final : FPCGExExportScratch
	{
		/** Resolved Property Collection values per actor: ExtractSchemaFromActor fires PreparePropertyValues, once. */
		TMap<const AActor*, TArray<FInstancedStruct>> PropertyValues;

		/** Properties already reported as not written, so an actor's splines report each once. */
		TSet<TPair<const AActor*, FName>> ReportedProperties;

		PCGExLevelExportShared::FAuthoredTags AuthoredTags;
	};
}

FPCGExExportSlotDesc UPCGExSplineExportHandler::GetSlotDesc() const
{
	FPCGExExportSlotDesc Desc;
	Desc.SlotId = PCGExLevelExport::Slots::Splines;
	Desc.PinName = PCGExCollections::Labels::SplinesPin;
	Desc.Scope = EPCGExExportSlotScope::None;
	return Desc;
}

void UPCGExSplineExportHandler::Collect(const FPCGExExportCandidate& Candidate, const FPCGExLevelExportSource& Source, const UPCGExLevelDataExporter* Exporter, FPCGExExportSlotWriter& Writer) const
{
	using namespace PCGExLevelExportSplineHandler;

	if (!Candidate.IsClaimedBy(PCGExLevelExport::Slots::Splines))
	{
		return;
	}

	const UPCGExDefaultLevelDataExporter* Default = Cast<UPCGExDefaultLevelDataExporter>(Exporter);
	PCGExLevelExport::FSplineExtractionOptions Options;
	Options.bConvertSplineMeshes = Default && Default->SplineClassificator && Default->SplineClassificator->bConvertSplineMeshes;
	Options.bWarnSkipped = true;

	AActor* Actor = Candidate.Actor;
	TArray<PCGExLevelExport::FExtractedSpline> Splines;
	PCGExLevelExport::ExtractSplines(Actor, Options, Splines);
	if (Splines.IsEmpty())
	{
		return;
	}

	FScratch& Scratch = Writer.GetOrCreateScratch<FScratch>();
	if (UPCGExPropertyCollectionComponent::FindOnActor(Actor))
	{
		Scratch.PropertyValues.Add(Actor, UPCGExPropertyCollectionComponent::ExtractSchemaFromActor(Actor));
	}

	// A component's own tags reach every spline it yields; ExtractSplines lists a component's splines together.
	const UActorComponent* TaggedComponent = nullptr;
	int32 TagSource = INDEX_NONE;
	for (const PCGExLevelExport::FExtractedSpline& Spline : Splines)
	{
		if (Spline.Component != TaggedComponent)
		{
			TaggedComponent = Spline.Component;
			TArray<FName> ComponentTags = TaggedComponent ? Scratch.AuthoredTags.OfComponent(TaggedComponent) : TArray<FName>();
			TagSource = ComponentTags.IsEmpty() ? INDEX_NONE : Writer.TagSources.Add(FPCGExExportSlotWriter::FTagSource{TaggedComponent->GetReadableName(), MoveTemp(ComponentTags)});
		}

		FPCGExExportItem& Item = Writer.AddItem(Source.ToFrame(Spline.Data->SplineStruct.Transform), FVector::ZeroVector, FVector::ZeroVector, Actor, INDEX_NONE);
		Item.TagSource = TagSource;
		Item.SourceComponent = Spline.Component;
		Item.Data.Reset(Spline.Data);
	}
}

void UPCGExSplineExportHandler::FinalizeItemData(UPCGData* Data, const int32 ItemIndex, FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
{
	using namespace PCGExLevelExportSplineHandler;

	UPCGSplineData* SplineData = Cast<UPCGSplineData>(Data);
	if (!SplineData)
	{
		return;
	}

	const FPCGExExportItem& Item = Writer.Items[ItemIndex];

	// Into the export frame: the control points stay local, the frame goes into the spline's own transform.
	PCGExPaths::Helpers::SetSplineTransform(SplineData, Item.Transform);

	if (Item.SourceComponent)
	{
		PCGExData::Helpers::SetDataValue<FString>(SplineData, ComponentNameAttribute, Item.SourceComponent->GetName());
	}

	FScratch& Scratch = Writer.GetOrCreateScratch<FScratch>();
	const TArray<FInstancedStruct>* Properties = Scratch.PropertyValues.Find(Item.SourceActor);
	if (!Properties)
	{
		return;
	}

	const UPCGMetadata* Metadata = SplineData->ConstMetadata();
	for (const FInstancedStruct& Instanced : *Properties)
	{
		const FPCGExProperty* Property = Instanced.GetPtr<FPCGExProperty>();
		if (!Property || !Property->SupportsOutput())
		{
			continue;
		}

		const FName OutName = Property->ResolveOutputAttributeName(PCGExMetaHelpers::SanitizeAttributeName(Property->PropertyName));
		const FName SidecarPin = Property->GetOutputSidecarPin();
		const bool bNameTaken = Metadata && Metadata->HasAttribute(FPCGAttributeIdentifier(OutName, PCGMetadataDomainID::Data));
		if (SidecarPin.IsNone() && !bNameTaken)
		{
			PCGExProperties::WriteDataDomainValue(SplineData, OutName, *Property);
			continue;
		}

		bool bAlreadyReported = false;
		Scratch.ReportedProperties.Add(TPair<const AActor*, FName>(Item.SourceActor, Property->PropertyName), &bAlreadyReported);
		if (bAlreadyReported)
		{
			continue;
		}

		const FString ActorName = Item.SourceActor ? Item.SourceActor->GetActorNameOrLabel() : FString();
		if (!SidecarPin.IsNone())
		{
			UE_LOG(LogPCGEx, Warning, TEXT("Level export: property '%s' of '%s' is not written on its splines: it needs its '%s' output, which a spline does not carry."),
			       *Property->PropertyName.ToString(), *ActorName, *SidecarPin.ToString());
		}
		else
		{
			UE_LOG(LogPCGEx, Warning, TEXT("Level export: property '%s' of '%s' is not written on its splines: the export writes its own '%s' attribute there."),
			       *Property->PropertyName.ToString(), *ActorName, *OutName.ToString());
		}
	}
}

void UPCGExSplineExportHandler::GetWrittenAttributeNames(const UPCGExLevelDataExporter* Exporter, const bool bRaw, TArray<FName>& OutNames) const
{
	OutNames.Add(PCGExLevelExportSplineHandler::ComponentNameAttribute);
}

#pragma endregion

#endif // WITH_EDITOR
