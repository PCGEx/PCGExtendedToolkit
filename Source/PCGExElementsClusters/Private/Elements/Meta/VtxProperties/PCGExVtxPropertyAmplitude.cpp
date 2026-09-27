// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/VtxProperties/PCGExVtxPropertyAmplitude.h"

#include "PCGExVersion.h"
#include "PCGPin.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Details/PCGExSettingsDetails.h"
#include "Types/PCGExTypes.h"


#define LOCTEXT_NAMESPACE "PCGExVtxPropertyAmplitude"
#define PCGEX_NAMESPACE PCGExVtxPropertyAmplitude

FPCGExAmplitudeConfig::FPCGExAmplitudeConfig()
{
	UpVector.Constant = PCGEX_CORE_SETTINGS.WorldUp;
	UpConstant_DEPRECATED = PCGEX_CORE_SETTINGS.WorldUp;
}

#if WITH_EDITOR
void FPCGExAmplitudeConfig::ApplyDeprecation()
{
	UpVector.Update(UpSelection_DEPRECATED, UpSource_DEPRECATED, UpConstant_DEPRECATED);
}

void FPCGExAmplitudeConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("UpSource")), FName(TEXT("UpVector")), FName(TEXT("Attribute")), FName(TEXT(" └─ Up Vector (Attr)")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("UpConstant")), FName(TEXT("UpVector")), FName(TEXT("Constant")), FName(TEXT(" └─ Up Vector")));
}
#endif

bool FPCGExAmplitudeConfig::Validate(FPCGExContext* InContext) const
{
#define PCGEX_VALIDATE_AMP_NAME(_NAME) if(bWrite##_NAME){ PCGEX_VALIDATE_NAME_C(InContext, _NAME##AttributeName) }

	PCGEX_VALIDATE_AMP_NAME(MinAmplitude)
	PCGEX_VALIDATE_AMP_NAME(MaxAmplitude)
	PCGEX_VALIDATE_AMP_NAME(AmplitudeRange)
	PCGEX_VALIDATE_AMP_NAME(AmplitudeSign)

#undef PCGEX_VALIDATE_AMP_NAME
	return true;
}

bool FPCGExVtxPropertyAmplitude::PrepareForCluster(FPCGExContext* InContext, TSharedPtr<PCGExClusters::FCluster> InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeDataFacade)
{
	if (!FPCGExVtxPropertyOperation::PrepareForCluster(InContext, InCluster, InVtxDataFacade, InEdgeDataFacade))
	{
		return false;
	}

	if (!Config.Validate(InContext))
	{
		bIsValidOperation = false;
		return false;
	}

	if (Config.bWriteAmplitudeSign && Config.UpMode == EPCGExVtxAmplitudeUpMode::UpVector)
	{
		DirCache = Config.UpVector.GetValueSetting();
		if (!DirCache->Init(InVtxDataFacade, false))
		{
			bIsValidOperation = false;
			return false;
		}
	}

	if (Config.bWriteMinAmplitude)
	{
		if (Config.MinMode == EPCGExVtxAmplitudeMode::Length)
		{
			MinAmpLengthBuffer = InVtxDataFacade->GetWritable<double>(Config.MinAmplitudeAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
		else
		{
			MinAmpBuffer = InVtxDataFacade->GetWritable<FVector>(Config.MinAmplitudeAttributeName, FVector::ZeroVector, true, PCGExData::EBufferInit::New);
		}
	}

	if (Config.bWriteMaxAmplitude)
	{
		if (Config.MaxMode == EPCGExVtxAmplitudeMode::Length)
		{
			MaxAmpLengthBuffer = InVtxDataFacade->GetWritable<double>(Config.MaxAmplitudeAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
		else
		{
			MaxAmpBuffer = InVtxDataFacade->GetWritable<FVector>(Config.MaxAmplitudeAttributeName, FVector::ZeroVector, true, PCGExData::EBufferInit::New);
		}
	}

	if (Config.bWriteAmplitudeRange)
	{
		if (Config.RangeMode == EPCGExVtxAmplitudeMode::Length)
		{
			AmpRangeLengthBuffer = InVtxDataFacade->GetWritable<double>(Config.AmplitudeRangeAttributeName, 0, true, PCGExData::EBufferInit::New);
		}
		else
		{
			AmpRangeBuffer = InVtxDataFacade->GetWritable<FVector>(Config.AmplitudeRangeAttributeName, FVector::ZeroVector, true, PCGExData::EBufferInit::New);
		}
	}

	if (Config.bWriteAmplitudeSign)
	{
		AmpSignBuffer = InVtxDataFacade->GetWritable<double>(Config.AmplitudeSignAttributeName, 0, true, PCGExData::EBufferInit::New);
		bUseSize = Config.SignOutputMode == EPCGExVtxAmplitudeSignOutput::Size;
	}

	return bIsValidOperation;
}

void FPCGExVtxPropertyAmplitude::ProcessNode(PCGExClusters::FNode& Node, const TArray<PCGExClusters::FAdjacencyData>& Adjacency, const PCGExMath::FBestFitPlane& BFP)
{
	const int32 NumAdjacency = Adjacency.Num();

	FVector AverageDirection = FVector::ZeroVector;
	FVector MinAmplitude = FVector(TNumericLimits<double>::Max());
	FVector MaxAmplitude = FVector(TNumericLimits<double>::Lowest());

	double MaxSize = 0;

	for (int i = 0; i < NumAdjacency; i++)
	{
		const PCGExClusters::FAdjacencyData& A = Adjacency[i];

		const FVector DirAndSize = A.Direction * A.Length;

		MaxSize = FMath::Max(MaxSize, bUseSize ? A.Length : 1);

		AverageDirection += A.Direction;
		MinAmplitude = PCGExTypeOps::FTypeOps<FVector>::Min(DirAndSize, MinAmplitude);
		MaxAmplitude = PCGExTypeOps::FTypeOps<FVector>::Max(DirAndSize, MaxAmplitude);
	}

	const FVector AmplitudeRange = MaxAmplitude - MinAmplitude;
	AverageDirection /= static_cast<double>(NumAdjacency);

	if (AmpSignBuffer)
	{
		double Sign = 0;

		if (Config.UpMode == EPCGExVtxAmplitudeUpMode::UpVector)
		{
			const FVector UpDir = DirCache->Read(Node.PointIndex);
			for (int i = 0; i < NumAdjacency; i++)
			{
				Sign += FVector::DotProduct(UpDir, Adjacency[i].Direction) * ((bUseSize ? Adjacency[i].Length : 1) / MaxSize);
			}
		}
		else
		{
			for (int i = 0; i < NumAdjacency; i++)
			{
				Sign += FVector::DotProduct(AverageDirection, Adjacency[i].Direction);
			}
		}

		Sign /= NumAdjacency;

		if (Config.SignOutputMode != EPCGExVtxAmplitudeSignOutput::Sign)
		{
			AmpSignBuffer->SetValue(Node.PointIndex, Config.bAbsoluteSign ? FMath::Abs(Sign) : Sign);
		}
		else
		{
			AmpSignBuffer->SetValue(Node.PointIndex, Config.bAbsoluteSign ? FMath::Abs(FMath::Sign(Sign)) : FMath::Sign(Sign));
		}
	}

	if (AmpRangeBuffer)
	{
		AmpRangeBuffer->SetValue(Node.PointIndex, Config.bAbsoluteRange ? PCGExTypes::Abs(AmplitudeRange) : AmplitudeRange);
	}
	if (AmpRangeLengthBuffer)
	{
		AmpRangeLengthBuffer->SetValue(Node.PointIndex, AmplitudeRange.Length());
	}

	if (MinAmpLengthBuffer)
	{
		MinAmpLengthBuffer->SetValue(Node.PointIndex, MinAmplitude.Length());
	}
	if (MinAmpBuffer)
	{
		MinAmpBuffer->SetValue(Node.PointIndex, MinAmplitude);
	}

	if (MaxAmpLengthBuffer)
	{
		MaxAmpLengthBuffer->SetValue(Node.PointIndex, MaxAmplitude.Length());
	}
	if (MaxAmpBuffer)
	{
		MaxAmpBuffer->SetValue(Node.PointIndex, MaxAmplitude);
	}
}

#if WITH_EDITOR
void UPCGExVtxPropertyAmplitudeSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.RenamePins(this, InOutNode);
		RetireInputPin(InOutNode, FName(TEXT("UpSelection")));
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExVtxPropertyAmplitudeSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 78, 2)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExVtxPropertyAmplitudeSettings::GetDisplayName() const
{
	return TEXT("");
}
#endif

TSharedPtr<FPCGExVtxPropertyOperation> UPCGExVtxPropertyAmplitudeFactory::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(VtxPropertyAmplitude)
	PCGEX_VTX_EXTRA_CREATE
	return NewOperation;
}

void UPCGExVtxPropertyAmplitudeFactory::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	if (Config.bWriteAmplitudeSign && Config.UpMode == EPCGExVtxAmplitudeUpMode::UpVector)
	{
		Config.UpVector.RegisterBufferDependencies(InContext, FacadePreloader);
	}
}

TArray<FPCGPinProperties> UPCGExVtxPropertyAmplitudeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	return PinProperties;
}

UPCGExFactoryData* UPCGExVtxPropertyAmplitudeSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExVtxPropertyAmplitudeFactory* NewFactory = InContext->ManagedObjects->New<UPCGExVtxPropertyAmplitudeFactory>();
	NewFactory->Config = Config;
	return Super::CreateFactory(InContext, NewFactory);
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
