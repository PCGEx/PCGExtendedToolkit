// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "Data/PCGExSubAccessor.h"
#include "Math/PCGExMathAxis.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Types/PCGExTypeOps.h"
#include "Types/PCGExTypeTraits.h"

class UPCGData;
struct FPCGAttributePropertyInputSelector;

namespace PCGExData
{
	class FPointIO;
	enum class EIOSide : uint8;
	class FFacade;
}

namespace PCGExData
{
	/**
	 * FSubSelection - Sub-selection configuration and type-erased operations
	 *
	 * This struct stores the configuration for selecting sub-components of values
	 * (like extracting .X from a vector, or Position from a Transform) and provides
	 * type-erased methods for applying the selection.
	 *
	 * Dispatch routes through FSubAccessorRegistry accessors (axis,
	 * transform-part, single-field) driven by the parsed chain's projection
	 * onto the legacy flag layout.
	 *
	 * Usage:
	 *   FSubSelection Sub(Selector);  // Parse selection from attribute path
	 *   
	 *   // Type-erased get (recommended)
	 *   double Result;
	 *   EPCGMetadataTypes ResultType;
	 *   Sub.GetVoid(SourceType, &SourceValue, &Result, ResultType);
	 *   
	 *   // Or for full extraction to working type
	 *   alignas(16) uint8 Buffer[96];
	 *   Sub.ApplyGet(SourceType, &SourceValue, Buffer, ResultType);
	 */
	struct PCGEXCORE_API FSubSelection
	{
		// Selection parameters. Populated by Init from the parsed chain. External
		// callers read these via the classifier methods below, not directly.
		PCGExTypeOps::ETransformPart Component = PCGExTypeOps::ETransformPart::Position;
		EPCGExAxis Axis = EPCGExAxis::Forward;
		PCGExTypeOps::ESingleField Field = PCGExTypeOps::ESingleField::X;
		EPCGMetadataTypes PossibleSourceType = EPCGMetadataTypes::Unknown;

		// Classifier bitmask. Cached at Init time so HasSelection /
		// IsFieldSelection / etc. are O(1) bit tests instead of chain walks.
		enum EClassifierBits : uint8
		{
			Bit_HasSelection   = 1 << 0,
			Bit_Field          = 1 << 1,
			Bit_Axis           = 1 << 2,
			Bit_Component      = 1 << 3,
			Bit_ContainerIndex = 1 << 4,
			Bit_ContainerCount = 1 << 5,
		};

		uint8 ClassifierMask = 0;

		// Constructors
		FSubSelection() = default;
		explicit FSubSelection(const TArray<FString>& ExtraNames);
		explicit FSubSelection(const FPCGAttributePropertyInputSelector& InSelector);
		explicit FSubSelection(const FString& Path, const UPCGData* InData = nullptr);

		/**
		 * Read-only access to the chain produced by Init. Empty when Init
		 * was given empty ExtraNames or when the FSubSelection was
		 * default-constructed and never re-Init'd.
		 *
		 * FCachedSubSelection's hot path uses the compiled chain (via
		 * CompileChainForSource); FSubSelection's type-erased path also
		 * walks the chain via accessor virtual calls.
		 */
		FORCEINLINE const FSubSelectionChain& GetChain() const
		{
			return ParsedChain;
		}

		//
		// Public classifier methods (chain-backed).
		//
		// These compute directly from ParsedChain via a classifier bitmask.
		// Malformed inputs like {Garbage} produce an empty chain and
		// HasSelection() returns false.
		//

		/** True if the parsed chain has at least one step. */
		FORCEINLINE bool HasSelection() const
		{
			return (ClassifierMask & Bit_HasSelection) != 0;
		}

		/** True if the chain contains a SingleField step (resolves to Double). */
		FORCEINLINE bool IsFieldSelection() const
		{
			return (ClassifierMask & Bit_Field) != 0;
		}

		/** True if the chain contains an Axis step. */
		FORCEINLINE bool IsAxisSelection() const
		{
			return (ClassifierMask & Bit_Axis) != 0;
		}

		/** True if the chain contains a TransformPart step. */
		FORCEINLINE bool IsComponentSelection() const
		{
			return (ClassifierMask & Bit_Component) != 0;
		}

		/** True if the chain contains an FContainerIndexAccessor step. */
		FORCEINLINE bool IsContainerIndexSelection() const
		{
			return (ClassifierMask & Bit_ContainerIndex) != 0;
		}

		/** True if the chain contains an FContainerCountAccessor step. */
		FORCEINLINE bool IsContainerCountSelection() const
		{
			return (ClassifierMask & Bit_ContainerCount) != 0;
		}

		/**
		 * Best-guess hint for the source-side type this selection assumes.
		 * E.g., ".R" hints Quaternion, ".X" hints Vector. Returns Unknown
		 * when the parser couldn't infer a hint (empty or unmatched tokens).
		 */
		FORCEINLINE EPCGMetadataTypes GetHintedSourceType() const
		{
			return PossibleSourceType;
		}

		/**
		 * Get the resulting type when this sub-selection is applied.
		 *
		 * - Field selection → Double
		 * - Axis selection → Vector
		 * - Component selection → Vector (Position/Scale) or Quaternion (Rotation)
		 * - ContainerCount selection → Double (the count)
		 * - ContainerIndex selection → Fallback (a container element is the
		 *   same type as the element type PCG reports via RealType)
		 * - No selection → Fallback (original type)
		 */
		EPCGMetadataTypes GetSubType(EPCGMetadataTypes Fallback = EPCGMetadataTypes::Unknown) const;

		void SetComponent(const PCGExTypeOps::ETransformPart InComponent);
		bool SetFieldIndex(const int32 InFieldIndex);

		friend FORCEINLINE uint32 GetTypeHash(const FSubSelection& S)
		{
			// Hash derives from the four selection-parameter fields plus the
			// chain shape (step count + each step's accessor kind). The old
			// booleans are gone so the hash recipe is slimmer; identical
			// inputs still produce identical hashes.
			uint32 Hash = 0;
			Hash = HashCombineFast(Hash, static_cast<uint32>(S.Axis));
			Hash = HashCombineFast(Hash, static_cast<uint32>(S.Field));
			Hash = HashCombineFast(Hash, static_cast<uint32>(S.Component));
			Hash = HashCombineFast(Hash, static_cast<uint32>(S.PossibleSourceType));
			Hash = HashCombineFast(Hash, static_cast<uint32>(S.ParsedChain.Steps.Num()));
			for (const FSubSelectionStep& Step : S.ParsedChain.Steps)
			{
				// Accessor pointer is stable for the process lifetime.
				Hash = HashCombineFast(Hash, PointerHash(Step.Accessor));
			}
			return Hash;
		}

	protected:
		void Init(const TArray<FString>& ExtraNames);

		/**
		 * Parsed chain. Populated by Init via FSubAccessorRegistry::ParseChain.
		 * Empty for default-constructed instances or empty ExtraNames.
		 */
		FSubSelectionChain ParsedChain;

		/**
		 * 1-entry compile cache for ApplyGet/ApplySet. The type-erased
		 * dispatch compiles the chain on-the-fly against SourceType; since
		 * the same FSubSelection is typically applied to many elements of
		 * the same type, caching the last compilation avoids re-running
		 * CompileChainForSource per element. Thread-safe via mutable
		 * (each FSubSelection is owned by a single proxy descriptor, not
		 * shared across threads).
		 */
		mutable FSubSelectionChain CachedCompiled;
		mutable EPCGMetadataTypes CachedCompiledSourceType = EPCGMetadataTypes::Unknown;

	public:
		//
		// Type-Erased Interface (Primary API)
		//
		// These methods dispatch through the sub-accessor system.
		// No template instantiation required at call sites.
		//

		/**
		 * Apply sub-selection when reading a value (Get)
		 * 
		 * Extracts the selected sub-component from the source and writes it
		 * to the output buffer. The output type depends on the selection:
		 * - Field → Double
		 * - Axis → Vector
		 * - Component → Vector or Quaternion
		 * - None → Same as source
		 * 
		 * @param SourceType Type of the source value
		 * @param Source Pointer to source value
		 * @param OutValue Pointer to output buffer (must be large enough for any type, recommend 96 bytes)
		 * @param OutType Receives the type of the output value
		 */
		void ApplyGet(EPCGMetadataTypes SourceType, const void* Source, void* OutValue, EPCGMetadataTypes& OutType) const;

		/**
		 * Apply sub-selection when writing a value (Set)
		 * 
		 * Sets the selected sub-component of the target from the source value.
		 * Handles conversion from source type to appropriate sub-component type.
		 * 
		 * @param TargetType Type of the target value
		 * @param Target Pointer to target value (modified in place)
		 * @param SourceType Type of the source value  
		 * @param Source Pointer to source value
		 */
		void ApplySet(EPCGMetadataTypes TargetType, void* Target, EPCGMetadataTypes SourceType, const void* Source) const;

		//
		// Legacy Type-Erased Interface (for compatibility)
		//
		// Matches the existing GetVoid signature but internally
		// uses the new system.
		//

		/**
		 * Type-erased Get with explicit working type
		 * (Legacy compatibility wrapper)
		 */
		void GetVoid(EPCGMetadataTypes SourceType, const void* Source, EPCGMetadataTypes WorkingType, void* Target) const;

		//
		// Templated Interface (for convenience, uses type-erased internally)
		//
		// This template is a thin wrapper that calls the type-erased method.
		// It does NOT create 14×14 instantiations - it just provides type safety
		// and calls the void* version.
		//

		template <typename TSource, typename TResult>
		TResult Get(const TSource& Value) const;
	};

	//
	// Template implementations - delegate to type-erased path
	//

	template <typename TSource, typename TResult>
	TResult FSubSelection::Get(const TSource& Value) const
	{
		TResult Result{};

		constexpr EPCGMetadataTypes SourceType = PCGExTypes::TTraits<TSource>::Type;
		constexpr EPCGMetadataTypes ResultType = PCGExTypes::TTraits<TResult>::Type;

		GetVoid(SourceType, &Value, ResultType, &Result);

		return Result;
	}

	PCGEXCORE_API bool TryGetType(const FPCGAttributePropertyInputSelector& InputSelector, const UPCGData* InData, EPCGMetadataTypes& OutType);
	PCGEXCORE_API bool TryGetTypeAndSource(const FPCGAttributePropertyInputSelector& InputSelector, const TSharedPtr<FFacade>& InDataFacade, EPCGMetadataTypes& OutType, EIOSide& InOutSide);
	PCGEXCORE_API bool TryGetTypeAndSource(const FName AttributeName, const TSharedPtr<FFacade>& InDataFacade, EPCGMetadataTypes& OutType, EIOSide& InOutSource);
}
