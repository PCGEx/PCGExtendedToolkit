// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Accessors/PCGExAxisAccessor.h"

#include "Math/PCGExMathAxis.h"

namespace PCGExData
{
	namespace PCGExAxisAccessor
	{
		// Mirrors STRMAP_AXIS from PCGExSubSelection.h.
		const TMap<FString, EPCGExAxis>& GetAxisTable()
		{
			static const TMap<FString, EPCGExAxis> Table = {
				{TEXT("FORWARD"), EPCGExAxis::Forward},
				{TEXT("FRONT"), EPCGExAxis::Forward},
				{TEXT("BACKWARD"), EPCGExAxis::Backward},
				{TEXT("BACK"), EPCGExAxis::Backward},
				{TEXT("RIGHT"), EPCGExAxis::Right},
				{TEXT("LEFT"), EPCGExAxis::Left},
				{TEXT("UP"), EPCGExAxis::Up},
				{TEXT("TOP"), EPCGExAxis::Up},
				{TEXT("DOWN"), EPCGExAxis::Down},
				{TEXT("BOTTOM"), EPCGExAxis::Down},
			};
			return Table;
		}

		//
		// ExtractAxis logic. All rotation types route through
		// PCGExMath::GetDirection.
		//

		FORCEINLINE FVector ExtractAxisFromQuat(const FQuat& Q, EPCGExAxis Axis)
		{
			return PCGExMath::GetDirection(Q, Axis);
		}

		FORCEINLINE FVector ExtractAxisFromRotator(const FRotator& R, EPCGExAxis Axis)
		{
			return PCGExMath::GetDirection(R.Quaternion(), Axis);
		}

		//
		// Typed fn pointers for compiled-chain hot path
		//

		void AxisGetStep_Quat(const void* Parent, void* ChildOut, const FAccessorParseResult& Parsed)
		{
			*static_cast<FVector*>(ChildOut) = ExtractAxisFromQuat(*static_cast<const FQuat*>(Parent), Parsed.Axis);
		}

		void AxisGetStep_Rotator(const void* Parent, void* ChildOut, const FAccessorParseResult& Parsed)
		{
			*static_cast<FVector*>(ChildOut) = ExtractAxisFromRotator(*static_cast<const FRotator*>(Parent), Parsed.Axis);
		}
	}

	bool FAxisAccessor::MatchesToken(const FString& UpperToken, FAccessorParseResult& OutParsed) const
	{
		if (const EPCGExAxis* Axis = PCGExAxisAccessor::GetAxisTable().Find(UpperToken))
		{
			OutParsed.Axis = *Axis;
			OutParsed.SourceTypeHint = EPCGMetadataTypes::Quaternion;
			return true;
		}
		return false;
	}

	bool FAxisAccessor::ResolveOutputType(EPCGMetadataTypes InType,
	                                      const FAccessorParseResult& Parsed,
	                                      EPCGMetadataTypes& OutType) const
	{
		(void)InType;
		(void)Parsed;
		OutType = EPCGMetadataTypes::Vector;
		return true;
	}

	void FAxisAccessor::ApplyGet(EPCGMetadataTypes InType,
	                             const void* Source,
	                             EPCGMetadataTypes OutType,
	                             void* OutValue,
	                             const FAccessorParseResult& Parsed) const
	{
		(void)OutType;
		check(Source != nullptr);
		check(OutValue != nullptr);

		FVector& Out = *static_cast<FVector*>(OutValue);

		switch (InType)
		{
		case EPCGMetadataTypes::Quaternion:
			Out = PCGExAxisAccessor::ExtractAxisFromQuat(*static_cast<const FQuat*>(Source), Parsed.Axis);
			break;
		case EPCGMetadataTypes::Rotator:
			Out = PCGExAxisAccessor::ExtractAxisFromRotator(*static_cast<const FRotator*>(Source), Parsed.Axis);
			break;
		case EPCGMetadataTypes::Transform:
			// FTransform: extract rotation quaternion, then apply axis.
			Out = PCGExAxisAccessor::ExtractAxisFromQuat(static_cast<const FTransform*>(Source)->GetRotation(), Parsed.Axis);
			break;
		default:
			Out = FVector::ForwardVector;
			break;
		}
	}

	FString FAxisAccessor::GetDisplayName() const
	{
		return TEXT("Axis");
	}

	FStepGetFn FAxisAccessor::GetStepGetFn(EPCGMetadataTypes InType) const
	{
		switch (InType)
		{
		case EPCGMetadataTypes::Quaternion:
			return &PCGExAxisAccessor::AxisGetStep_Quat;
		case EPCGMetadataTypes::Rotator:
			return &PCGExAxisAccessor::AxisGetStep_Rotator;
		default:
			return nullptr;
		}
	}

	ISubAccessor::ECompileAction FAxisAccessor::ClassifyForInType(
		EPCGMetadataTypes InType, const FAccessorParseResult& Parsed,
		const FPCGMetadataAttributeDesc* SourceDesc) const
	{
		(void)Parsed;
		(void)SourceDesc;
		switch (InType)
		{
		case EPCGMetadataTypes::Quaternion:
		case EPCGMetadataTypes::Rotator:
			return ECompileAction::Keep;

		case EPCGMetadataTypes::Transform:
			return ECompileAction::PromoteWithRotation;

		default:
			return ECompileAction::Drop;
		}
	}
}
