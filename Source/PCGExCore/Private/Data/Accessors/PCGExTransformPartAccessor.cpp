// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Accessors/PCGExTransformPartAccessor.h"

#include "Types/PCGExTypeOpsImpl.h"

namespace PCGExData
{
	namespace PCGExTransformPartAccessor
	{
		// Mirrors STRMAP_TRANSFORM_FIELD from PCGExSubSelection.h.
		struct FComponentEntry
		{
			PCGExTypeOps::ETransformPart Component;
			EPCGMetadataTypes Hint;
		};

		const TMap<FString, FComponentEntry>& GetComponentTable()
		{
			static const TMap<FString, FComponentEntry> Table = {
				{TEXT("POSITION"), {PCGExTypeOps::ETransformPart::Position, EPCGMetadataTypes::Vector}},
				{TEXT("POS"), {PCGExTypeOps::ETransformPart::Position, EPCGMetadataTypes::Vector}},
				{TEXT("ROTATION"), {PCGExTypeOps::ETransformPart::Rotation, EPCGMetadataTypes::Quaternion}},
				{TEXT("ROT"), {PCGExTypeOps::ETransformPart::Rotation, EPCGMetadataTypes::Quaternion}},
				{TEXT("ORIENT"), {PCGExTypeOps::ETransformPart::Rotation, EPCGMetadataTypes::Quaternion}},
				{TEXT("SCALE"), {PCGExTypeOps::ETransformPart::Scale, EPCGMetadataTypes::Vector}},
			};
			return Table;
		}

		EPCGMetadataTypes ComponentToOutputType(PCGExTypeOps::ETransformPart Part)
		{
			switch (Part)
			{
			case PCGExTypeOps::ETransformPart::Position:
				return EPCGMetadataTypes::Vector;
			case PCGExTypeOps::ETransformPart::Rotation:
				return EPCGMetadataTypes::Quaternion;
			case PCGExTypeOps::ETransformPart::Scale:
				return EPCGMetadataTypes::Vector;
			}
			return EPCGMetadataTypes::Unknown;
		}
	}

	bool FTransformPartAccessor::MatchesToken(const FString& UpperToken, FAccessorParseResult& OutParsed) const
	{
		if (const PCGExTransformPartAccessor::FComponentEntry* Entry = PCGExTransformPartAccessor::GetComponentTable().Find(UpperToken))
		{
			OutParsed.Component = Entry->Component;
			OutParsed.SourceTypeHint = Entry->Hint;
			return true;
		}
		return false;
	}

	bool FTransformPartAccessor::ResolveOutputType(EPCGMetadataTypes InType,
	                                               const FAccessorParseResult& Parsed,
	                                               EPCGMetadataTypes& OutType) const
	{
		// This accessor only meaningfully applies to FTransform sources.
		// The chain may include this step on a non-Transform source (the
		// projection path tolerates it; the compiler will drop it).
		OutType = PCGExTransformPartAccessor::ComponentToOutputType(Parsed.Component);
		return InType == EPCGMetadataTypes::Transform;
	}

	void FTransformPartAccessor::ApplyGet(EPCGMetadataTypes InType,
	                                      const void* Source,
	                                      EPCGMetadataTypes OutType,
	                                      void* OutValue,
	                                      const FAccessorParseResult& Parsed) const
	{
		(void)OutType;
		check(Source != nullptr);
		check(OutValue != nullptr);
		check(InType == EPCGMetadataTypes::Transform);

		const FTransform& T = *static_cast<const FTransform*>(Source);
		switch (Parsed.Component)
		{
		case PCGExTypeOps::ETransformPart::Position:
			*static_cast<FVector*>(OutValue) = T.GetLocation();
			break;
		case PCGExTypeOps::ETransformPart::Rotation:
			*static_cast<FQuat*>(OutValue) = T.GetRotation();
			break;
		case PCGExTypeOps::ETransformPart::Scale:
			*static_cast<FVector*>(OutValue) = T.GetScale3D();
			break;
		}
	}

	void FTransformPartAccessor::ApplySet(EPCGMetadataTypes InType,
	                                      void* TargetInOut,
	                                      EPCGMetadataTypes SourceType,
	                                      const void* Source,
	                                      const FAccessorParseResult& Parsed) const
	{
		check(TargetInOut != nullptr);
		check(Source != nullptr);
		check(InType == EPCGMetadataTypes::Transform);

		FTransform& T = *static_cast<FTransform*>(TargetInOut);

		// Mirrors PCGExData::SubSelectionImpl::InjectTransformComponent
		// (PCGExSubSelectionOpsImpl.h:47-59). Type checks are deliberate -- a
		// mismatched source type silently no-ops (legacy behavior).
		switch (Parsed.Component)
		{
		case PCGExTypeOps::ETransformPart::Position:
			if (SourceType == EPCGMetadataTypes::Vector)
			{
				T.SetLocation(*static_cast<const FVector*>(Source));
			}
			break;
		case PCGExTypeOps::ETransformPart::Rotation:
			if (SourceType == EPCGMetadataTypes::Quaternion)
			{
				T.SetRotation(*static_cast<const FQuat*>(Source));
			}
			else if (SourceType == EPCGMetadataTypes::Rotator)
			{
				T.SetRotation(static_cast<const FRotator*>(Source)->Quaternion());
			}
			break;
		case PCGExTypeOps::ETransformPart::Scale:
			if (SourceType == EPCGMetadataTypes::Vector)
			{
				T.SetScale3D(*static_cast<const FVector*>(Source));
			}
			break;
		}
	}

	FString FTransformPartAccessor::GetDisplayName() const
	{
		return TEXT("TransformPart");
	}

	//
	// Typed fn pointers for compiled-chain hot path
	//

	namespace PCGExTransformPartAccessor
	{
		void TransformGetStep(const void* Parent, void* ChildOut, const FAccessorParseResult& Parsed)
		{
			const FTransform& T = *static_cast<const FTransform*>(Parent);
			switch (Parsed.Component)
			{
			case PCGExTypeOps::ETransformPart::Position:
				*static_cast<FVector*>(ChildOut) = T.GetLocation();
				break;
			case PCGExTypeOps::ETransformPart::Rotation:
				*static_cast<FQuat*>(ChildOut) = T.GetRotation();
				break;
			case PCGExTypeOps::ETransformPart::Scale:
				*static_cast<FVector*>(ChildOut) = T.GetScale3D();
				break;
			}
		}

		void TransformSetStep(void* ParentInOut, const void* NewChild, const FAccessorParseResult& Parsed)
		{
			FTransform& T = *static_cast<FTransform*>(ParentInOut);
			switch (Parsed.Component)
			{
			case PCGExTypeOps::ETransformPart::Position:
				T.SetLocation(*static_cast<const FVector*>(NewChild));
				break;
			case PCGExTypeOps::ETransformPart::Rotation:
				T.SetRotation(*static_cast<const FQuat*>(NewChild));
				break;
			case PCGExTypeOps::ETransformPart::Scale:
				T.SetScale3D(*static_cast<const FVector*>(NewChild));
				break;
			}
		}
	}

	FStepGetFn FTransformPartAccessor::GetStepGetFn(EPCGMetadataTypes InType) const
	{
		return InType == EPCGMetadataTypes::Transform ? &PCGExTransformPartAccessor::TransformGetStep : nullptr;
	}

	FStepSetFn FTransformPartAccessor::GetStepSetFn(EPCGMetadataTypes InType) const
	{
		return InType == EPCGMetadataTypes::Transform ? &PCGExTransformPartAccessor::TransformSetStep : nullptr;
	}

	ISubAccessor::ECompileAction FTransformPartAccessor::ClassifyForInType(
		EPCGMetadataTypes InType, const FAccessorParseResult& Parsed,
		const FPCGMetadataAttributeDesc* SourceDesc) const
	{
		(void)Parsed;
		(void)SourceDesc;
		// Position/Rotation/Scale only make sense on a Transform source.
		return InType == EPCGMetadataTypes::Transform ? ECompileAction::Keep : ECompileAction::Drop;
	}
}
