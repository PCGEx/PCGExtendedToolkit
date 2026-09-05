// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Debug/PCGExDrawAttributes.h"

#include "DrawDebugHelpers.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGExPointIO.h"
#include "Math/PCGExMath.h"

#define LOCTEXT_NAMESPACE "PCGExDrawAttributes"
#define PCGEX_NAMESPACE DrawAttributes

PCGExData::EIOInit UPCGExDrawAttributesSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

#if WITH_EDITOR
FString FPCGExAttributeDebugDrawConfig::GetDisplayName() const
{
	if (bEnabled)
	{
		return FPCGExInputConfig::GetDisplayName();
	}
	return "(Disabled) " + FPCGExInputConfig::GetDisplayName();
}
#endif

bool FPCGExAttributeDebugDraw::Bind(const TSharedRef<PCGExData::FPointIO>& PointIO)
{
	bValid = false;

	VectorGetter.Reset();
	IndexGetter.Reset();
	SingleGetter.Reset();
	SizeGetter.Reset();
	ColorGetter.Reset();
	TextGetter.Reset();

	switch (Config->ExpressedAs)
	{
	case EPCGExDebugExpression::Direction:
	case EPCGExDebugExpression::Point:
	case EPCGExDebugExpression::ConnectionToPosition:
		VectorGetter = MakeShared<PCGExData::TAttributeBroadcaster<FVector>>();
		bValid = VectorGetter->Prepare(Config->Selector, PointIO);
		if (bValid)
		{
			VectorGetter->Grab();
		}
		break;
	case EPCGExDebugExpression::ConnectionToIndex:
		IndexGetter = MakeShared<PCGExData::TAttributeBroadcaster<int32>>();
		bValid = IndexGetter->Prepare(Config->Selector, PointIO);
		if (bValid)
		{
			IndexGetter->Grab();
		}
		break;
	case EPCGExDebugExpression::Boolean:
		SingleGetter = MakeShared<PCGExData::TAttributeBroadcaster<double>>();
		bValid = SingleGetter->Prepare(Config->Selector, PointIO);
		if (bValid)
		{
			SingleGetter->Grab();
		}
		break;
	default: ;
	}

	if (bValid)
	{
		if (Config->bSizeFromAttribute)
		{
			SizeGetter = MakeShared<PCGExData::TAttributeBroadcaster<double>>();
			SizeGetter->Prepare(Config->Selector, PointIO);
			SizeGetter->Grab();
		}

		if (Config->bColorFromAttribute)
		{
			ColorGetter = MakeShared<PCGExData::TAttributeBroadcaster<FVector>>();
			ColorGetter->Prepare(Config->LocalColorAttribute, PointIO);
			ColorGetter->Grab();
		}
	}

	return bValid;
}

double FPCGExAttributeDebugDraw::GetSize(const PCGExData::FConstPoint& Point) const
{
	double Value = Config->Size;
	if (Config->bSizeFromAttribute && SizeGetter)
	{
		Value = SizeGetter->Values[Point.Index] * Config->Size;
	}
	return Value;
}

FColor FPCGExAttributeDebugDraw::GetColor(const PCGExData::FConstPoint& Point) const
{
	if (ColorGetter)
	{
		const FVector Value = ColorGetter->Values[Point.Index];
		return Config->bColorIsLinear ? FColor(Value.X * 255.0f, Value.Y * 255.0f, Value.Z * 255.0f) : FColor(Value.X, Value.Y, Value.Z);
	}
	return Config->Color;
}

double FPCGExAttributeDebugDraw::GetSingle(const PCGExData::FConstPoint& Point) const
{
	return SingleGetter->Values[Point.Index];
}

FVector FPCGExAttributeDebugDraw::GetVector(const PCGExData::FConstPoint& Point) const
{
	FVector OutVector = VectorGetter->Values[Point.Index];
	if (Config->ExpressedAs == EPCGExDebugExpression::Direction && Config->bNormalizeBeforeSizing)
	{
		OutVector.Normalize();
	}
	return OutVector;
}

FVector FPCGExAttributeDebugDraw::GetIndexedPosition(const PCGExData::FConstPoint& Point, const UPCGBasePointData* PointData) const
{
	if (const int64 OutIndex = IndexGetter->Values[Point.Index];
		OutIndex != -1)
	{
		return PointData->GetTransform(PCGExMath::Tile<int32>(OutIndex, 0, PointData->GetNumPoints() - 1)).GetLocation();
	}
	return Point.GetTransform().GetLocation();
}

void FPCGExAttributeDebugDraw::Draw(const UWorld* World, const FVector& Start, const PCGExData::FConstPoint& Point, const UPCGBasePointData* PointData) const
{
	switch (Config->ExpressedAs)
	{
	case EPCGExDebugExpression::Direction:
		DrawDirection(World, Start, Point);
		break;
	case EPCGExDebugExpression::ConnectionToIndex:
		DrawConnection(World, Start, Point, GetIndexedPosition(Point, PointData));
		break;
	case EPCGExDebugExpression::ConnectionToPosition:
		DrawConnection(World, Start, Point, GetVector(Point));
		break;
	case EPCGExDebugExpression::Point:
		DrawPoint(World, Start, Point);
		break;
	case EPCGExDebugExpression::Boolean:
		DrawSingle(World, Start, Point);
		break;
	default: ;
	}
}

void FPCGExAttributeDebugDraw::DrawDirection(const UWorld* World, const FVector& Start, const PCGExData::FConstPoint& Point) const
{
#if WITH_EDITOR
	const FVector Dir = GetVector(Point) * GetSize(Point);
	DrawDebugDirectionalArrow(World, Start, Start + Dir, Dir.Length() * 0.05f, GetColor(Point), true, -1, Config->DepthPriority, Config->Thickness);
#endif
}

void FPCGExAttributeDebugDraw::DrawConnection(const UWorld* World, const FVector& Start, const PCGExData::FConstPoint& Point, const FVector& End) const
{
#if WITH_EDITOR
	DrawDebugLine(World, Start, Config->bAsOffset ? Start + End : End, GetColor(Point), true, -1, Config->DepthPriority, Config->Thickness);
#endif
}

void FPCGExAttributeDebugDraw::DrawPoint(const UWorld* World, const FVector& Start, const PCGExData::FConstPoint& Point) const
{
#if WITH_EDITOR
	const FVector End = GetVector(Point);
	DrawDebugPoint(World, Config->bAsOffset ? Start + End : End, GetSize(Point), GetColor(Point), true, -1, Config->DepthPriority);
#endif
}

void FPCGExAttributeDebugDraw::DrawSingle(const UWorld* World, const FVector& Start, const PCGExData::FConstPoint& Point) const
{
#if WITH_EDITOR
	const double End = GetSingle(Point);
	DrawDebugPoint(World, Start, GetSize(Point), End <= 0 ? Config->SecondaryColor : GetColor(Point), true, -1, Config->DepthPriority);
#endif
}

UPCGExDrawAttributesSettings::UPCGExDrawAttributesSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	DebugSettings.PointScale = 0.0f;
	if (DebugList.IsEmpty())
	{
		FPCGExAttributeDebugDrawConfig& Forward = DebugList.Emplace_GetRef();
		Forward.Selector.Update(TEXT("$transform.Forward"));
		Forward.Color = FColor::Red;
		Forward.Size = 50;

		FPCGExAttributeDebugDrawConfig& Right = DebugList.Emplace_GetRef();
		Right.Selector.Update(TEXT("$transform.Right"));
		Right.Color = FColor::Green;
		Right.Size = 50;

		FPCGExAttributeDebugDrawConfig& Up = DebugList.Emplace_GetRef();
		Up.Selector.Update(TEXT("$transform.Up"));
		Up.Color = FColor::Blue;
		Up.Size = 50;
	}
#endif
}

#if WITH_EDITOR
void UPCGExDrawAttributesSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	DebugSettings.PointScale = 0.0f;
	for (FPCGExAttributeDebugDrawConfig& Config : DebugList)
	{
		Config.UpdateUserFacingInfos();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

PCGEX_INITIALIZE_ELEMENT(DrawAttributes)

bool FPCGExDrawAttributesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

#if WITH_EDITOR
	PCGEX_CONTEXT_AND_SETTINGS(DrawAttributes)

	if (!Settings->bPCGExDebug)
	{
		return false;
	}

	Context->DebugList.Empty();
	for (const FPCGExAttributeDebugDrawConfig& Config : Settings->DebugList)
	{
		FPCGExAttributeDebugDrawConfig& MutableConfig = (const_cast<FPCGExAttributeDebugDrawConfig&>(Config));

		if (!Config.bEnabled)
		{
			continue;
		}

		FPCGExAttributeDebugDraw& Drawer = Context->DebugList.Emplace_GetRef();
		Drawer.Config = &MutableConfig;
	}

	if (Context->DebugList.IsEmpty())
	{
		PCGE_LOG(Warning, GraphAndLog, FTEXT("Debug list is empty."));
	}

#endif

	return true;
}

bool FPCGExDrawAttributesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExDrawAttributesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(DrawAttributes)

#if WITH_EDITOR

	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->SetState(PCGExCommon::States::State_ReadyForNextPoints);
	}

	PCGEX_ON_STATE(PCGExCommon::States::State_ReadyForNextPoints)
	{
		if (!Context->AdvancePointsIO())
		{
			Context->Done();
		}
		else
		{
			Context->SetState(PCGExCommon::States::State_ProcessingPoints);
		}
	}

	PCGEX_ON_STATE(PCGExCommon::States::State_ProcessingPoints)
	{
		for (FPCGExAttributeDebugDraw& DebugInfos : Context->DebugList)
		{
			DebugInfos.Bind(Context->CurrentIO.ToSharedRef());
		}

		const UWorld* World = Context->GetWorld();

		for (int PointIndex = 0; PointIndex < Context->CurrentIO->GetNum(); PointIndex++)
		{
			const PCGExData::FConstPoint& Point = Context->CurrentIO->GetInPoint(PointIndex);
			const FVector Start = Point.GetTransform().GetLocation();
			DrawDebugPoint(World, Start, 1.0f, FColor::White, true);
			for (FPCGExAttributeDebugDraw& Drawer : Context->DebugList)
			{
				if (!Drawer.bValid)
				{
					continue;
				}
				Drawer.Draw(World, Start, Point, Context->CurrentIO->GetIn());
			}
		}

		Context->CurrentIO->ClearCachedKeys();
		Context->SetState(PCGExCommon::States::State_ReadyForNextPoints);
	}

	if (Context->IsDone())
	{
		DisabledPassThroughData(Context);
	}

	return Context->IsDone();

#else

	DisabledPassThroughData(Context); return true;

#endif
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
