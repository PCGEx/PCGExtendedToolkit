// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Utils/PCGExIterations.h"


#include "PCGGraph.h"
#include "PCGParamData.h"
#include "PCGExVersion.h"
#include "PCGPin.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSplineData.h"
#include "Data/PCGTextureData.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"
#define PCGEX_NAMESPACE Iterations

#pragma region UPCGSettings interface

TArray<FPCGPinProperties> UPCGExIterationsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExIterationsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(FName("Iterations"));
	Pin.SetRequiredPin();

#if PCGEX_ENGINE_VERSION < 507
	switch (Type)
	{
	default:
	case EPCGExIterationDataType::Any: Pin.AllowedTypes = EPCGDataType::Any;
		break;
	case EPCGExIterationDataType::Params: Pin.AllowedTypes = EPCGDataType::Param;
		break;
	case EPCGExIterationDataType::Points: Pin.AllowedTypes = EPCGDataType::Point;
		break;
	case EPCGExIterationDataType::Spline: Pin.AllowedTypes = EPCGDataType::Spline;
		break;
	case EPCGExIterationDataType::Texture: Pin.AllowedTypes = EPCGDataType::BaseTexture;
		break;
	}
#else
	switch (Type)
	{
	default: case EPCGExIterationDataType::Any: Pin.AllowedTypes = FPCGDataTypeInfo::AsId();
		break;
	case EPCGExIterationDataType::Params: Pin.AllowedTypes = FPCGDataTypeInfoParam::AsId();
		break;
	case EPCGExIterationDataType::Points: Pin.AllowedTypes = FPCGDataTypeInfoPoint::AsId();
		break;
	case EPCGExIterationDataType::Spline: Pin.AllowedTypes = FPCGDataTypeInfoSpline::AsId();
		break;
	case EPCGExIterationDataType::Texture: Pin.AllowedTypes = FPCGDataTypeInfoTexture2D::AsId();
		break;
	}
#endif

	return PinProperties;
}

FPCGElementPtr UPCGExIterationsSettings::CreateElement() const { return MakeShared<FPCGExIterationsElement>(); }

#pragma endregion

bool FPCGExIterationsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT()
	PCGEX_SETTINGS(Iterations)

	const FName OutputLabel = FName("Iterations");

	const int32 NumIterations = FMath::Max(0, Settings->Iterations);
	FString NumIterationsTag = FString::Printf(TEXT("NumIterations:%u"), NumIterations);
	Context->IncreaseStagedOutputReserve(NumIterations);

	if (Settings->bOutputUtils && Settings->Type == EPCGExIterationDataType::Params)
	{
		for (int i = 0; i < NumIterations; i++)
		{
			UPCGParamData* Data = Context->ManagedObjects->New<UPCGParamData>();
			UPCGMetadata* Metadata = Data->Metadata;

			Metadata->FindOrCreateAttribute<int32>(FName("Iteration"), i);
			Metadata->FindOrCreateAttribute<int32>(FName("NumIterations"), NumIterations);
			const double Progress = static_cast<double>(i) / static_cast<double>(NumIterations - 1);
			Metadata->FindOrCreateAttribute<double>(FName("OneMinusProgress"), 1 - Progress);
			Metadata->FindOrCreateAttribute<double>(FName("Progress"), Progress);

			Metadata->AddEntry();

			Context->StageOutput(
				Data, OutputLabel, PCGExData::EStaging::None,
				{FString::Printf(TEXT("Iteration:%u"), i), NumIterationsTag});
		}
	}
	else
	{
		UPCGData* Data = nullptr;

		switch (Settings->Type)
		{
		default: case EPCGExIterationDataType::Params: Data = Context->ManagedObjects->New<UPCGParamData>();
			break;
		case EPCGExIterationDataType::Points: Data = Context->ManagedObjects->New<UPCGPointArrayData>();
			break;
		case EPCGExIterationDataType::Spline: Data = Context->ManagedObjects->New<UPCGSplineData>();
			break;
		case EPCGExIterationDataType::Texture: Data = Context->ManagedObjects->New<UPCGTextureData>();
			break;
		}

		for (int i = 0; i < NumIterations; i++)
		{
			Context->StageOutput(
				Data, OutputLabel, PCGExData::EStaging::None,
				{FString::Printf(TEXT("Iteration:%u"), i), NumIterationsTag});
		}
	}

	Context->Done();
	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
