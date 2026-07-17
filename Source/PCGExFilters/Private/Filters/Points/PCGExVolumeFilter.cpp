// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExVolumeFilter.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGVolumeData.h"
#include "Details/PCGExSettingsDetails.h"
#include "GameFramework/Volume.h"
#include "Math/PCGExMathBounds.h"

#define LOCTEXT_NAMESPACE "PCGExVolumeFilterDefinition"
#define PCGEX_NAMESPACE PCGExVolumeFilterDefinition

#pragma region UPCGExVolumeFilterFactory

bool UPCGExVolumeFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}

	bAllShorthandsConstant = Config.ExtraRadius.CanSupportDataOnly()
		&& (!Config.bUsePenetrationThreshold || Config.PenetrationThreshold.CanSupportDataOnly());

	return true;
}

bool UPCGExVolumeFilterFactory::DomainCheck()
{
	return Config.ExtraRadius.CanSupportDataOnly()
		&& (!Config.bUsePenetrationThreshold || Config.PenetrationThreshold.CanSupportDataOnly());
}

bool UPCGExVolumeFilterFactory::SupportsProxyEvaluation() const
{
	return bAllShorthandsConstant;
}

bool UPCGExVolumeFilterFactory::SupportsCollectionEvaluation() const
{
	return bOnlyUseDataDomain;
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExVolumeFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FVolumeFilter>(this);
}

PCGExFactories::EPreparationResult UPCGExVolumeFilterFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}

	const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(FName("Volumes"));

	for (const FPCGTaggedData& TaggedData : Inputs)
	{
		const UPCGVolumeData* VolumeData = Cast<UPCGVolumeData>(TaggedData.Data);
		if (!VolumeData)
		{
			continue;
		}

		const TWeakObjectPtr<AVolume> VolumeActor = VolumeData->GetVolumeActor();
		if (!VolumeActor.IsValid())
		{
			continue;
		}

		PCGExPointFilter::FCachedVolume& Entry = CachedVolumes.AddDefaulted_GetRef();
		Entry.WorldBounds = VolumeData->GetBounds();
		Entry.VolumeActor = VolumeActor;
	}

	if (CachedVolumes.IsEmpty())
	{
		if (MissingDataPolicy == EPCGExFilterNoDataFallback::Error)
		{
			PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("Missing volume data."))
		}
		return PCGExFactories::EPreparationResult::MissingData;
	}

	// Build octree from cached volume bounds
	FBox OctreeBounds(ForceInit);
	for (const PCGExPointFilter::FCachedVolume& Entry : CachedVolumes)
	{
		OctreeBounds += Entry.WorldBounds;
	}

	Octree = MakeShared<PCGExOctree::FItemOctree>(OctreeBounds.GetCenter(), OctreeBounds.GetExtent().Length());
	for (int32 i = 0; i < CachedVolumes.Num(); i++)
	{
		Octree->AddElement(PCGExOctree::FItem(i, CachedVolumes[i].WorldBounds));
	}

	return Result;
}

void UPCGExVolumeFilterFactory::BeginDestroy()
{
	CachedVolumes.Empty();
	Octree.Reset();
	Super::BeginDestroy();
}

#pragma endregion

#pragma region FVolumeFilter

bool PCGExPointFilter::FVolumeFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	CachedVolumes = &TypedFilterFactory->CachedVolumes;
	Octree = TypedFilterFactory->Octree.Get();
	if (!CachedVolumes || CachedVolumes->IsEmpty() || !Octree)
	{
		return false;
	}

	const FPCGExVolumeFilterConfig& Cfg = TypedFilterFactory->Config;
	CheckType = Cfg.CheckType;
	BoundsSource = Cfg.BoundsSource;
	RadiusSource = Cfg.RadiusSource;
	bInvert = Cfg.bInvert;
	bUsePenetrationThreshold = Cfg.bUsePenetrationThreshold;
	PenetrationMode = Cfg.PenetrationMode;

	ExtraRadius = Cfg.ExtraRadius.GetValueSetting();
	ExtraRadius->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
	if (!ExtraRadius->Init(InPointDataFacade))
	{
		return false;
	}

	if (bUsePenetrationThreshold)
	{
		PenetrationThresholdValue = Cfg.PenetrationThreshold.GetValueSetting();
		PenetrationThresholdValue->bRegisterConsumable &= TypedFilterFactory->bCleanupConsumableAttributes;
		if (!PenetrationThresholdValue->Init(InPointDataFacade))
		{
			return false;
		}
	}

	return true;
}

double PCGExPointFilter::FVolumeFilter::GetEffectiveRadius(const FBox& LocalBox, const int32 PointIndex) const
{
	const double BoundsRadius = ComputeRadius(LocalBox, RadiusSource);
	const double Radius = BoundsRadius + ExtraRadius->Read(PointIndex);

	if (!bUsePenetrationThreshold)
	{
		return Radius;
	}

	const double Threshold = PenetrationThresholdValue->Read(PointIndex);
	return ComputeEffectiveRadius(Radius, true, PenetrationMode, Threshold);
}

bool PCGExPointFilter::FVolumeFilter::TestPoint(const FVector& Position, const double EffectiveRadius) const
{
	return TestPoint(Position, EffectiveRadius, *Octree, *CachedVolumes, CheckType, bInvert);
}

bool PCGExPointFilter::FVolumeFilter::TestPoint(const FVector& Position, const double EffectiveRadius, const PCGExOctree::FItemOctree& InOctree, const TArray<FCachedVolume>& InVolumes, const EPCGExVolumeCheckType InCheckType, const bool bInInvert) const
{
	bool bFoundInside = false;
	bool bMatched = false;

	InOctree.FindElementsWithBoundsTest(
		FBoxCenterAndExtent(Position, FVector(EffectiveRadius)),
		[&](const PCGExOctree::FItem& Item)
		{
			if (bMatched)
			{
				return;
			} // Already resolved for non-aggregate checks

			const FCachedVolume& Volume = InVolumes[Item.Index];
			if (!Volume.VolumeActor.IsValid())
			{
				return;
			}

			float DistToSurface = 0.0f;
			const bool bSphereOverlaps = Volume.VolumeActor->EncompassesPoint(Position, static_cast<float>(EffectiveRadius), &DistToSurface);
			const bool bPointInside = DistToSurface < KINDA_SMALL_NUMBER;

			switch (InCheckType)
			{
			case EPCGExVolumeCheckType::IsInside:
				if (bPointInside)
				{
					bMatched = true;
				}
				break;

			case EPCGExVolumeCheckType::Intersects:
				if (bSphereOverlaps && !bPointInside)
				{
					bMatched = true;
				}
				break;

			case EPCGExVolumeCheckType::IsInsideOrIntersects:
				if (bSphereOverlaps)
				{
					bMatched = true;
				}
				break;

			case EPCGExVolumeCheckType::IsOutsideOrIntersects:
				if (bSphereOverlaps && !bPointInside)
				{
					bMatched = true;
				}
				else if (bPointInside)
				{
					bFoundInside = true;
				}
				break;
			}
		});

	if (bMatched)
	{
		return !bInInvert;
	}

	// No volume matched
	if (InCheckType == EPCGExVolumeCheckType::IsOutsideOrIntersects)
	{
		return bFoundInside ? bInInvert : !bInInvert;
	}

	return bInInvert;
}

bool PCGExPointFilter::FVolumeFilter::Test(const PCGExData::FProxyPoint& Point) const
{
	const FVector Position = Point.GetTransform().GetLocation();
	const FBox LocalBox = PCGExMath::GetLocalBounds(Point, BoundsSource);
	const double BoundsRadius = ComputeRadius(LocalBox, RadiusSource);
	const double Radius = BoundsRadius + ExtraRadius->Read(0);
	const double EffRadius = bUsePenetrationThreshold
		? ComputeEffectiveRadius(Radius, true, PenetrationMode, PenetrationThresholdValue->Read(0))
		: Radius;
	return TestPoint(Position, EffRadius);
}

bool PCGExPointFilter::FVolumeFilter::Test(const int32 PointIndex) const
{
	const PCGExData::FConstPoint Point = PointDataFacade->Source->GetInPoint(PointIndex);
	const FVector Position = Point.GetTransform().GetLocation();
	const FBox LocalBox = PCGExMath::GetLocalBounds(Point, BoundsSource);
	return TestPoint(Position, GetEffectiveRadius(LocalBox, PointIndex));
}

bool PCGExPointFilter::FVolumeFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	// Self-contained collection-level eval (see IFilter::Test(IO, ParentCollection) contract): the
	// volume octree is factory data built in Prepare() -- independent of the per-point Init() -- and
	// the radius shorthands are read per-IO, so each data is judged on its own @Data values rather
	// than the seed facade's. Stays correct even when the per-point Init() failed.
	const TArray<FCachedVolume>& Volumes = TypedFilterFactory->CachedVolumes;
	const PCGExOctree::FItemOctree* VolumeOctree = TypedFilterFactory->Octree.Get();
	if (Volumes.IsEmpty() || !VolumeOctree)
	{
		PCGEX_QUIET_HANDLING_RET
	}

	const FPCGExVolumeFilterConfig& Cfg = TypedFilterFactory->Config;

	double ExtraR = 0;
	if (!Cfg.ExtraRadius.TryReadDataValue(IO, ExtraR, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	double Threshold = 0;
	if (Cfg.bUsePenetrationThreshold && !Cfg.PenetrationThreshold.TryReadDataValue(IO, Threshold, PCGEX_QUIET_HANDLING))
	{
		PCGEX_QUIET_HANDLING_RET
	}

	PCGExData::FProxyPoint ProxyPoint;
	IO->GetDataAsProxyPoint(ProxyPoint);

	const FVector Position = ProxyPoint.GetTransform().GetLocation();
	const FBox LocalBox = PCGExMath::GetLocalBounds(ProxyPoint, Cfg.BoundsSource);
	const double Radius = ComputeRadius(LocalBox, Cfg.RadiusSource) + ExtraR;
	const double EffRadius = ComputeEffectiveRadius(Radius, Cfg.bUsePenetrationThreshold, Cfg.PenetrationMode, Threshold);

	return TestPoint(Position, EffRadius, *VolumeOctree, Volumes, Cfg.CheckType, Cfg.bInvert);
}

#pragma endregion

#pragma region UPCGExVolumeFilterProviderSettings

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGExVolumeFilterProviderSettings::GetPreconfiguredInfo() const
{
	const TSet<EPCGExVolumeCheckType> ValuesToSkip = {};
	return FPCGPreConfiguredSettingsInfo::PopulateFromEnum<EPCGExVolumeCheckType>(ValuesToSkip, FTEXT("{0} (Volume)"));
}
#endif

void UPCGExVolumeFilterProviderSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfigureInfo);
	if (const UEnum* EnumPtr = StaticEnum<EPCGExVolumeCheckType>())
	{
		if (EnumPtr->IsValidEnumValue(PreconfigureInfo.PreconfiguredIndex))
		{
			Config.CheckType = static_cast<EPCGExVolumeCheckType>(PreconfigureInfo.PreconfiguredIndex);
		}
	}
}

TArray<FPCGPinProperties> UPCGExVolumeFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_VOLUMES(FName("Volumes"), TEXT("Volume data to test points against"), Required)
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(Volume)

#if WITH_EDITOR
FString UPCGExVolumeFilterProviderSettings::GetDisplayName() const
{
	switch (Config.CheckType)
	{
	default:
	case EPCGExVolumeCheckType::IsInside:
		return TEXT("Is Inside");
	case EPCGExVolumeCheckType::Intersects:
		return TEXT("Intersects");
	case EPCGExVolumeCheckType::IsInsideOrIntersects:
		return TEXT("Is Inside or Intersects");
	case EPCGExVolumeCheckType::IsOutsideOrIntersects:
		return TEXT("Is Outside or Intersects");
	}
}
#endif

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
