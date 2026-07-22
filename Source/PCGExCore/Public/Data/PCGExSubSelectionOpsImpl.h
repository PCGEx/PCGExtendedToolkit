// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSubSelectionOps.h"
#include "Types/PCGExTypeOpsImpl.h"

/**
 * PCGEx Sub-Selection Operations Implementation
 * 
 * Contains the templated TSubSelectorOpsImpl<T> that bridges the type-specific
 * operations to the ISubSelectorOps interface.
 */

namespace PCGExData
{
	/**
	 * Field extraction functions - static implementations per type
	 * 
	 * These are the actual extraction implementations. Only 14 of each function
	 * exist (one per type), not 14×14 combinations.
	 */
	namespace SubSelectionFunctions
	{
		//
		// Transform component operations
		//

		FORCEINLINE void ExtractTransformComponent(const FTransform& Transform, PCGExTypeOps::ETransformPart Part, void* OutValue, EPCGMetadataTypes& OutType)
		{
			switch (Part)
			{
			case PCGExTypeOps::ETransformPart::Position:
				*static_cast<FVector*>(OutValue) = Transform.GetLocation();
				OutType = EPCGMetadataTypes::Vector;
				break;
			case PCGExTypeOps::ETransformPart::Rotation:
				*static_cast<FQuat*>(OutValue) = Transform.GetRotation();
				OutType = EPCGMetadataTypes::Quaternion;
				break;
			case PCGExTypeOps::ETransformPart::Scale:
				*static_cast<FVector*>(OutValue) = Transform.GetScale3D();
				OutType = EPCGMetadataTypes::Vector;
				break;
			}
		}

		FORCEINLINE void InjectTransformComponent(FTransform& Transform, PCGExTypeOps::ETransformPart Part, const void* Value, EPCGMetadataTypes ValueType)
		{
			switch (Part)
			{
			case PCGExTypeOps::ETransformPart::Position:
				if (ValueType == EPCGMetadataTypes::Vector)
				{
					Transform.SetLocation(*static_cast<const FVector*>(Value));
				}
				break;
			case PCGExTypeOps::ETransformPart::Rotation:
				if (ValueType == EPCGMetadataTypes::Quaternion)
				{
					Transform.SetRotation(*static_cast<const FQuat*>(Value));
				}
				else if (ValueType == EPCGMetadataTypes::Rotator)
				{
					Transform.SetRotation(static_cast<const FRotator*>(Value)->Quaternion());
				}
				break;
			case PCGExTypeOps::ETransformPart::Scale:
				if (ValueType == EPCGMetadataTypes::Vector)
				{
					Transform.SetScale3D(*static_cast<const FVector*>(Value));
				}
				break;
			}
		}
	}

	/**
	 * TSubSelectorOpsImpl<T> - Per-type implementation of ISubSelectorOps
	 * 
	 * Only 14 instantiations exist (one per supported type).
	 * All sub-selection logic is contained here, eliminating the need for
	 * 14×14 template combinations.
	 */
	template <typename T>
	class TSubSelectorOpsImpl : public ISubSelectorOps
	{
	public:
		using Traits = PCGExTypes::TTraits<T>;
		using SubSelectionTraits = TSubSelectionTraits<T>;

		//~ Begin ISubSelectorOps interface

		virtual EPCGMetadataTypes GetTypeId() const override
		{
			return Traits::Type;
		}

		virtual int32 GetNumFields() const override
		{
			return SubSelectionTraits::NumFields;
		}

		virtual bool SupportsFieldExtraction() const override
		{
			return SubSelectionTraits::bSupportsFieldExtraction;
		}

		virtual bool SupportsAxisExtraction() const override
		{
			return SubSelectionTraits::bSupportsAxisExtraction;
		}

		virtual bool SupportsComponentExtraction() const override
		{
			return SubSelectionTraits::bSupportsComponentExtraction;
		}

		virtual double ExtractField(const void* Value, PCGExTypeOps::ESingleField Field) const override
		{
			return PCGExTypeOps::FTypeOps<T>::ExtractField(Value, Field);
		}

		virtual void InjectField(void* Target, double Value, PCGExTypeOps::ESingleField Field) const override
		{
			PCGExTypeOps::FTypeOps<T>::InjectField(Target, Value, Field);
		}

		virtual FVector ExtractAxis(const void* Value, EPCGExAxis Axis) const override
		{
			if constexpr (PCGExTypes::TTraits<T>::bIsRotation)
			{
				return PCGExTypeOps::FTypeOps<T>::ExtractAxis(Value, Axis);
			}
			else
			{
				return FVector::ForwardVector;
			}
		}

		virtual void ExtractComponent(const void* Transform, PCGExTypeOps::ETransformPart Part, void* OutValue, EPCGMetadataTypes& OutType) const override
		{
			if constexpr (std::is_same_v<T, FTransform>)
			{
				PCGExTypeOps::FTypeOps<T>::ExtractComponent(Transform, Part, OutValue, OutType);
			}
		}

		virtual void InjectComponent(void* Transform, PCGExTypeOps::ETransformPart Part, const void* Value, EPCGMetadataTypes ValueType) const override
		{
			if constexpr (std::is_same_v<T, FTransform>)
			{
				PCGExTypeOps::FTypeOps<T>::InjectComponent(Transform, Part, Value, ValueType);
			}
		}

		virtual void ApplyGetSelection(const void* Value, const FSubSelection& Selection, void* OutValue, EPCGMetadataTypes& OutType) const override
		{
			ApplyGetSelectionImpl(*static_cast<const T*>(Value), Selection, OutValue, OutType);
		}

		virtual void ApplySetSelection(void* Target, const FSubSelection& Selection, const void* Source, EPCGMetadataTypes SourceType) const override
		{
			ApplySetSelectionImpl(*static_cast<T*>(Target), Selection, Source, SourceType);
		}

		//~ End ISubSelectorOps interface

		// Static instance accessor (for registry)
		static TSubSelectorOpsImpl<T>& GetInstance()
		{
			static TSubSelectorOpsImpl<T> Instance;
			return Instance;
		}

	private:
		//
		// Apply selection - high-level operations that use the above primitives
		//

		void ApplyGetSelectionImpl(const T& Value, const FSubSelection& Selection, void* OutValue, EPCGMetadataTypes& OutType) const
		{
			if (!Selection.bIsValid)
			{
				// No sub-selection - copy the whole value
				*static_cast<T*>(OutValue) = Value;
				OutType = Traits::Type;
				return;
			}

			// Handle component extraction first (Transform only)
			if constexpr (std::is_same_v<T, FTransform>)
			{
				if (Selection.bIsComponentSet)
				{
					// Extract the component first
					if (Selection.Component == PCGExTypeOps::ETransformPart::Rotation)
					{
						FQuat Rotation;
						EPCGMetadataTypes ComponentType;
						PCGExTypeOps::FTypeOps<FTransform>::ExtractQuat(&Value, Selection.Component, &Rotation, ComponentType);

						// Then apply axis or field selection if needed
						if (Selection.bIsAxisSet)
						{
							*static_cast<FVector*>(OutValue) = PCGExTypeOps::FTypeOps<FQuat>::ExtractAxis(&Rotation, Selection.Axis);
							OutType = EPCGMetadataTypes::Vector;
						}
						else if (Selection.bIsFieldSet)
						{
							*static_cast<double*>(OutValue) = PCGExTypeOps::FTypeOps<FQuat>::ExtractField(&Rotation, Selection.Field);
							OutType = EPCGMetadataTypes::Double;
						}
						else
						{
							*static_cast<FQuat*>(OutValue) = Rotation;
							OutType = EPCGMetadataTypes::Quaternion;
						}
					}
					else // Position or Scale
					{
						FVector Vec;
						EPCGMetadataTypes ComponentType;
						PCGExTypeOps::FTypeOps<FTransform>::ExtractVector(&Value, Selection.Component, &Vec, ComponentType);

						if (Selection.bIsFieldSet)
						{
							*static_cast<double*>(OutValue) = PCGExTypeOps::FTypeOps<FVector>::ExtractField(&Vec, Selection.Field);
							OutType = EPCGMetadataTypes::Double;
						}
						else
						{
							*static_cast<FVector*>(OutValue) = Vec;
							OutType = EPCGMetadataTypes::Vector;
						}
					}
					return;
				}
			}

			// Handle axis extraction (rotation types)
			if (Selection.bIsAxisSet)
			{
				// TODO : This needs to be different based on the selected component
				*static_cast<FVector*>(OutValue) = ExtractAxis(&Value, Selection.Axis);
				OutType = EPCGMetadataTypes::Vector;
				return;
			}

			// Handle field extraction
			if (Selection.bIsFieldSet)
			{
				*static_cast<double*>(OutValue) = PCGExTypeOps::FTypeOps<T>::ExtractField(&Value, Selection.Field);
				OutType = EPCGMetadataTypes::Double;
				return;
			}

			// Fallback - copy whole value
			*static_cast<T*>(OutValue) = Value;
			OutType = Traits::Type;
		}

		void ApplySetSelectionImpl(T& Target, const FSubSelection& Selection, const void* Source, EPCGMetadataTypes SourceType) const
		{
			if (!Selection.bIsValid)
			{
				// No sub-selection - need to convert and copy the whole value
				PCGExTypeOps::FConversionTable::Convert(SourceType, Source, Traits::Type, &Target);
				return;
			}

			// Handle component injection (Transform only)
			if constexpr (std::is_same_v<T, FTransform>)
			{
				if (Selection.bIsComponentSet)
				{
					if (Selection.Component == PCGExTypeOps::ETransformPart::Rotation)
					{
						if (Selection.bIsFieldSet)
						{
							// Get the current rotation, modify one field
							FQuat Rotation = Target.GetRotation();
							double ScalarValue = 0.0;
							PCGExTypeOps::FConversionTable::Convert(SourceType, Source, EPCGMetadataTypes::Double, &ScalarValue);
							PCGExTypeOps::FTypeOps<FQuat>::InjectField(&Rotation, ScalarValue, Selection.Field);
							Target.SetRotation(Rotation);
						}
						else
						{
							// Set the whole rotation
							PCGExTypeOps::FTypeOps<FTransform>::InjectComponent(&Target, Selection.Component, Source, SourceType);
						}
					}
					else // Position or Scale
					{
						if (Selection.bIsFieldSet)
						{
							FVector Vec = (Selection.Component == PCGExTypeOps::ETransformPart::Position) ? Target.GetLocation() : Target.GetScale3D();
							double ScalarValue = 0.0;
							PCGExTypeOps::FConversionTable::Convert(SourceType, Source, EPCGMetadataTypes::Double, &ScalarValue);
							PCGExTypeOps::FTypeOps<FVector>::InjectField(&Vec, ScalarValue, Selection.Field);

							if (Selection.Component == PCGExTypeOps::ETransformPart::Position)
							{
								Target.SetLocation(Vec);
							}
							else
							{
								Target.SetScale3D(Vec);
							}
						}
						else
						{
							PCGExTypeOps::FTypeOps<FTransform>::InjectComponent(&Target, Selection.Component, Source, SourceType);
						}
					}
					return;
				}
			}

			// Handle field injection for multi-component types
			if (Selection.bIsFieldSet)
			{
				double ScalarValue = 0.0;
				PCGExTypeOps::FConversionTable::Convert(SourceType, Source, EPCGMetadataTypes::Double, &ScalarValue);
				PCGExTypeOps::FTypeOps<T>::InjectField(&Target, ScalarValue, Selection.Field);
				return;
			}

			// Fallback - convert and copy
			PCGExTypeOps::FConversionTable::Convert(SourceType, Source, Traits::Type, &Target);
		}
	};

	// Extern template declarations (instantiated in cpp)
#define PCGEX_DECLARE_SUBSELECTOR_OPS_EXTERN(_TYPE, _NAME, ...) \
	extern template class TSubSelectorOpsImpl<_TYPE>;
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_DECLARE_SUBSELECTOR_OPS_EXTERN)
#undef PCGEX_DECLARE_SUBSELECTOR_OPS_EXTERN

	// FSubSelectorRegistry template implementation
	template <typename T>
	const ISubSelectorOps* FSubSelectorRegistry::Get()
	{
		return &TSubSelectorOpsImpl<T>::GetInstance();
	}
}
