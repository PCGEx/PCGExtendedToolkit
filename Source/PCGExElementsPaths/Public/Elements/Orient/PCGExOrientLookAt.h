// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExOrientOperation.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "PCGExOrientLookAt.generated.h"

UENUM()
enum class EPCGExOrientLookAtMode : uint8
{
	NextPoint     = 0 UMETA(DisplayName = "Next Point", ToolTip="Look at next point in path"),
	PreviousPoint = 1 UMETA(DisplayName = "Previous Point", ToolTip="Look at previous point in path"),
	Direction     = 2 UMETA(DisplayName = "Direction", ToolTip="Use a local vector attribute as a direction to look at"),
	Position      = 3 UMETA(DisplayName = "Position", ToolTip="Use a local vector attribtue as a world position to look at"),
};


class FPCGExOrientLookAt : public FPCGExOrientOperation
{
public:
	EPCGExOrientLookAtMode LookAt = EPCGExOrientLookAtMode::NextPoint;
	FPCGAttributePropertyInputSelector LookAtAttribute;

	virtual bool PrepareForData(const TSharedRef<PCGExData::FFacade>& InDataFacade, const TSharedRef<PCGExPaths::FPath>& InPath) override
	{
		if (!FPCGExOrientOperation::PrepareForData(InDataFacade, InPath))
		{
			return false;
		}

		if (LookAt == EPCGExOrientLookAtMode::Direction || LookAt == EPCGExOrientLookAtMode::Position)
		{
			LookAtGetter = InDataFacade->GetBroadcaster<FVector>(LookAtAttribute, true);
			if (!LookAtGetter)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("LookAt Attribute ({0}) is not valid."), FText::FromString(PCGExMetaHelpers::GetSelectorDisplayName(LookAtAttribute))));
				return false;
			}
		}

		return true;
	}

	virtual FTransform ComputeOrientation(const PCGExData::FConstPoint& Point, const double DirectionMultiplier) const override
	{
		switch (LookAt)
		{
		default: case EPCGExOrientLookAtMode::NextPoint:
			return LookAtAxis(Point.GetTransform(), Path->DirToNextPoint(Point.Index), DirectionMultiplier);
		case EPCGExOrientLookAtMode::PreviousPoint:
			return LookAtAxis(Point.GetTransform(), Path->DirToPrevPoint(Point.Index), DirectionMultiplier);
		case EPCGExOrientLookAtMode::Direction:
			return LookAtDirection(Point.GetTransform(), Point.Index, DirectionMultiplier);
		case EPCGExOrientLookAtMode::Position:
			return LookAtPosition(Point.GetTransform(), Point.Index, DirectionMultiplier);
		}
	}

	virtual FTransform LookAtAxis(const FTransform& InT, const FVector& InAxis, const double DirectionMultiplier) const
	{
		FTransform OutT = InT;
		OutT.SetRotation(PCGExMath::MakeDirection(Factory->OrientAxis, InAxis * DirectionMultiplier, PCGExMath::GetDirection(Factory->UpAxis)));
		return OutT;
	}

	virtual FTransform LookAtDirection(const FTransform& InT, const int32 Index, const double DirectionMultiplier) const
	{
		FTransform OutT = InT;
		OutT.SetRotation(PCGExMath::MakeDirection(Factory->OrientAxis, LookAtGetter->Read(Index).GetSafeNormal() * DirectionMultiplier, PCGExMath::GetDirection(Factory->UpAxis)));
		return OutT;
	}

	virtual FTransform LookAtPosition(const FTransform& InT, const int32 Index, const double DirectionMultiplier) const
	{
		FTransform OutT = InT;
		const FVector Current = OutT.GetLocation();
		const FVector Position = LookAtGetter->Read(Index);
		OutT.SetRotation(PCGExMath::MakeDirection(Factory->OrientAxis, (Position - Current).GetSafeNormal() * DirectionMultiplier, PCGExMath::GetDirection(Factory->UpAxis)));
		return OutT;
	}

protected:
	TSharedPtr<PCGExData::TBuffer<FVector>> LookAtGetter;
};

/**
 * 
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Look At", PCGExNodeLibraryDoc="paths/transform/path-orient/orient-look-at"))
class UPCGExOrientLookAt : public UPCGExOrientInstancedFactory
{
	GENERATED_BODY()

public:
	/** Look at method */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExOrientLookAtMode LookAt = EPCGExOrientLookAtMode::NextPoint;

	/** Vector attribute representing either a direction or world position, depending on selected mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="LookAt == EPCGExOrientLookAtMode::Direction || LookAt == EPCGExOrientLookAtMode::Position", EditConditionHides))
	FPCGAttributePropertyInputSelector LookAtAttribute;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override
	{
		Super::CopySettingsFrom(Other);
		if (const UPCGExOrientLookAt* TypedOther = Cast<UPCGExOrientLookAt>(Other))
		{
			LookAt = TypedOther->LookAt;
			LookAtAttribute = TypedOther->LookAtAttribute;
		}
	}

	virtual TSharedPtr<FPCGExOrientOperation> CreateOperation() const override
	{
		PCGEX_FACTORY_NEW_OPERATION(OrientLookAt)
		NewOperation->Factory = this;
		NewOperation->LookAt = LookAt;
		NewOperation->LookAtAttribute = LookAtAttribute;
		return NewOperation;
	}
};
