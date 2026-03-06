// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "PCGExOpStats.h"
#include "PCGExVersion.h"
#include "Data/PCGExProxyData.h"
#include "Metadata/PCGMetadataAttributeTraits.h"

#if PCGEX_ENGINE_VERSION > 506
#include "PCGPointPropertiesTraits.h"
#else
namespace PCGExMT
{
	struct FScope;
}
#include "PCGCommon.h"
#endif

struct FPCGExContext;
class UPCGBasePointData;
enum class EPCGExABBlendingType : uint8;

namespace PCGExMath
{
	class IDistances;
}

namespace PCGEx
{
	class FIndexLookup;
}

namespace PCGExDetails
{
	class FDistances;
}

namespace PCGExData
{
	enum class EIOSide : uint8;
	class IBuffer;
	class FFacade;
	struct FWeightedPoint;
	class IBufferProxy;
	class IUnionData;

	struct FProxyDescriptor;
}

namespace PCGExMT
{
	struct FScope;
}

namespace PCGExBlending
{
	struct FBlendingParam;
	class IBlendOperation;
	//
	// IBlender - Base interface for multi-attribute blending
	//
	class PCGEXBLENDING_API IBlender : public TSharedFromThis<IBlender>
	{
	public:
		virtual ~IBlender() = default;

		// Target = Target|Target
		FORCEINLINE virtual void Blend(const int32 TargetIndex, const double Weight) const
		{
			Blend(TargetIndex, TargetIndex, TargetIndex, Weight);
		}

		// Target = Source|Target
		FORCEINLINE virtual void Blend(const int32 SourceIndex, const int32 TargetIndex, const double Weight) const
		{
			Blend(SourceIndex, TargetIndex, TargetIndex, Weight);
		}

		virtual void InitTrackers(TArray<PCGEx::FOpStats>& Trackers) const = 0;

		// Target = SourceA|SourceB
		virtual void Blend(const int32 SourceIndexA, const int32 SourceIndexB, const int32 TargetIndex, const double Weight) const = 0;

		virtual void BeginMultiBlend(const int32 TargetIndex, TArray<PCGEx::FOpStats>& Trackers) const = 0;
		virtual void MultiBlend(const int32 SourceIndex, const int32 TargetIndex, const double Weight, TArray<PCGEx::FOpStats>& Tracker) const = 0;
		virtual void EndMultiBlend(const int32 TargetIndex, TArray<PCGEx::FOpStats>& Tracker) const = 0;
	};

	//
	// FDummyBlender - No-op blender implementation
	//
	class PCGEXBLENDING_API FDummyBlender final : public IBlender
	{
	public:
		virtual ~FDummyBlender() override = default;

		virtual void Blend(const int32 TargetIndex, const double Weight) const override
		{
		}

		virtual void Blend(const int32 SourceIndex, const int32 TargetIndex, const double Weight) const override
		{
		}

		virtual void InitTrackers(TArray<PCGEx::FOpStats>& Trackers) const override
		{
		}

		virtual void Blend(const int32 SourceIndexA, const int32 SourceIndexB, const int32 TargetIndex, const double Weight) const override
		{
		}

		virtual void BeginMultiBlend(const int32 TargetIndex, TArray<PCGEx::FOpStats>& Trackers) const override
		{
		}

		virtual void MultiBlend(const int32 SourceIndex, const int32 TargetIndex, const double Weight, TArray<PCGEx::FOpStats>& Tracker) const override
		{
		}

		FORCEINLINE virtual void EndMultiBlend(const int32 TargetIndex, TArray<PCGEx::FOpStats>& Tracker) const override
		{
		}
	};

	//
	// IUnionBlender - Interface for union-based multi-source blending
	//
	class PCGEXBLENDING_API IUnionBlender : public TSharedFromThis<IUnionBlender>
	{
	public:
		virtual ~IUnionBlender() = default;

		virtual void InitTrackers(TArray<PCGEx::FOpStats>& Trackers) const = 0;
		virtual int32 ComputeWeights(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const = 0;
		virtual void Blend(const int32 WriteIndex, const TArray<PCGExData::FWeightedPoint>& InWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const = 0;
		virtual void MergeSingle(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const = 0;
		virtual void MergeSingle(const int32 UnionIndex, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const = 0;

		FORCEINLINE EPCGPointNativeProperties GetAllocatedProperties() const { return AllocatedProperties; }

	protected:
		EPCGPointNativeProperties AllocatedProperties = EPCGPointNativeProperties::None;
	};

	//
	// FDummyUnionBlender - Minimal union blender for weight computation only
	//
	class PCGEXBLENDING_API FDummyUnionBlender final : public IUnionBlender
	{
	public:
		virtual ~FDummyUnionBlender() override = default;

		void Init(const TSharedPtr<PCGExData::FFacade>& TargetData, const TArray<TSharedRef<PCGExData::FFacade>>& InSources);

		virtual void InitTrackers(TArray<PCGEx::FOpStats>& Trackers) const override
		{
		}

		virtual int32 ComputeWeights(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints) const override;

		virtual void Blend(const int32 WriteIndex, const TArray<PCGExData::FWeightedPoint>& InWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const override
		{
		}

		virtual void MergeSingle(const int32 WriteIndex, const TSharedPtr<PCGExData::IUnionData>& InUnionData, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const override
		{
		}

		virtual void MergeSingle(const int32 UnionIndex, TArray<PCGExData::FWeightedPoint>& OutWeightedPoints, TArray<PCGEx::FOpStats>& Trackers) const override
		{
		}

	protected:
		TSharedPtr<PCGExData::FFacade> CurrentTargetData;
		TSharedPtr<PCGEx::FIndexLookup> IOLookup;
		TArray<const UPCGBasePointData*> SourcesData;
		const PCGExMath::IDistances* Distances = nullptr;
	};

	//
	// FProxyDataBlender - Type-erased A×B→C blender using function pointers
	//
	// This is a simplified, runtime-polymorphic blender that uses IBlendOperation
	// for all blending logic. No more template explosion.
	//
	// Replaces: IProxyDataBlender<T> and TProxyDataBlender<T, MODE, bool>
	//
	class PCGEXBLENDING_API FProxyDataBlender : public TSharedFromThis<FProxyDataBlender>
	{
	public:
		virtual ~FProxyDataBlender() = default;

		// Underlying type info
		EPCGMetadataTypes UnderlyingType = EPCGMetadataTypes::Unknown;

		// Buffer proxies
		TSharedPtr<PCGExData::IBufferProxy> A;
		TSharedPtr<PCGExData::IBufferProxy> B;
		TSharedPtr<PCGExData::IBufferProxy> C; // Output

		// Blend operation (holds function pointers)
		TSharedPtr<IBlendOperation> Operation;

		// Target = Source|Target
		FORCEINLINE void Blend(const int32 SourceIndex, const int32 TargetIndex, const double Weight)
		{
			Blend(SourceIndex, TargetIndex, TargetIndex, Weight);
		}

		// Target = SourceA|SourceB
		void Blend(const int32 SourceIndexA, const int32 SourceIndexB, const int32 TargetIndex, const double Weight) const;

		// 1:1 Range blending
		void BlendScope(const PCGExMT::FScope& Scope, const double Weight) const;
		void BlendScope(const PCGExMT::FScope& Scope, TArrayView<const double> Weights) const;
		void BlendScope(const PCGExMT::FScope& Scope, TArrayView<const int8> Mask, const double Weight) const;
		void BlendScope(const PCGExMT::FScope& Scope, TArrayView<const int8> Mask, TArrayView<const double> Weights) const;

		// Multi-blend operations
		PCGEx::FOpStats BeginMultiBlend(const int32 TargetIndex);
		void MultiBlend(const int32 SourceIndex, const int32 TargetIndex, const double Weight, PCGEx::FOpStats& Tracker);
		void EndMultiBlend(const int32 TargetIndex, PCGEx::FOpStats& Tracker);

		// Division helper
		void Div(const int32 TargetIndex, const double Divider);

		// Get output buffer
		TSharedPtr<PCGExData::IBuffer> GetOutputBuffer() const;

		// Initialize from blending param (helper for common setup pattern)
		bool InitFromParam(
			FPCGExContext* InContext,
			const FBlendingParam& InParam,
			const TSharedPtr<PCGExData::FFacade> InTargetFacade,
			const TSharedPtr<PCGExData::FFacade> InSourceFacade,
			PCGExData::EIOSide InSide,
			PCGExData::EProxyFlags InProxyFlags = PCGExData::EProxyFlags::None);

		// Type-safe set (converts to working type)
		//template <typename T>
		//void Set(const int32 TargetIndex, const T& Value) const { if (C) { C->Set(TargetIndex, Value); } }

	protected:
		friend PCGEXBLENDING_API TSharedPtr<FProxyDataBlender> CreateProxyBlender(EPCGMetadataTypes, EPCGExABBlendingType, bool);
		friend PCGEXBLENDING_API TSharedPtr<FProxyDataBlender> CreateProxyBlender(FPCGExContext*, EPCGExABBlendingType, const PCGExData::FProxyDescriptor&, const PCGExData::FProxyDescriptor&, const PCGExData::FProxyDescriptor&, bool);
		friend PCGEXBLENDING_API TSharedPtr<FProxyDataBlender> CreateProxyBlender(FPCGExContext*, EPCGExABBlendingType, const PCGExData::FProxyDescriptor&, const PCGExData::FProxyDescriptor&, bool);

		// Cached type info
		bool bNeedsLifecycleManagement = false;

		// Cached value size/alignment for working buffers (from Operation)
		int32 ValueSize = 0;
		int32 ValueAlignment = 1;

		// Create a FScopedTypedValue with correct sizing for the underlying type.
		// Uses the 3-arg constructor when ValueSize is known (generic/new types),
		// otherwise falls back to the 1-arg constructor (known legacy types).
		FORCEINLINE PCGExTypes::FScopedTypedValue MakeScopedValue() const
		{
			if (ValueSize > 0 && PCGExTypes::FScopedTypedValue::GetTypeSize(UnderlyingType) == 0)
			{
				return PCGExTypes::FScopedTypedValue(UnderlyingType, ValueSize, ValueAlignment);
			}
			return PCGExTypes::FScopedTypedValue(UnderlyingType);
		}
	};

	//
	// Factory functions for creating proxy blenders
	//

	// Create blender with just type and mode - caller sets A, B, C proxies manually
	// This replaces the old CreateProxyBlender<T>(BlendMode, bReset) template
	PCGEXBLENDING_API TSharedPtr<FProxyDataBlender> CreateProxyBlender(
		EPCGMetadataTypes WorkingType,
		EPCGExABBlendingType BlendMode,
		bool bResetValueForMultiBlend = true);

	// Create blender with A, B, and C descriptors
	PCGEXBLENDING_API TSharedPtr<FProxyDataBlender> CreateProxyBlender(
		FPCGExContext* InContext,
		const EPCGExABBlendingType BlendMode,
		const PCGExData::FProxyDescriptor& A,
		const PCGExData::FProxyDescriptor& B,
		const PCGExData::FProxyDescriptor& C,
		const bool bResetValueForMultiBlend = true);

	// Create blender with A and C descriptors (B = null, uses C for reading current value)
	PCGEXBLENDING_API TSharedPtr<FProxyDataBlender> CreateProxyBlender(
		FPCGExContext* InContext,
		const EPCGExABBlendingType BlendMode,
		const PCGExData::FProxyDescriptor& A,
		const PCGExData::FProxyDescriptor& C,
		const bool bResetValueForMultiBlend = true);
}
