// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExBlendOperations.h"

#include "Core/PCGExOpStats.h"

namespace PCGExBlending
{
	IBlendOperation::IBlendOperation(const EPCGExABBlendingType InMode, const bool bInResetForMulti)
		: Mode(InMode),
		  bResetForMulti(bInResetForMulti),
		  bInitWithSource(
			  InMode == EPCGExABBlendingType::Min ||
			  InMode == EPCGExABBlendingType::Max ||
			  InMode == EPCGExABBlendingType::UnsignedMin ||
			  InMode == EPCGExABBlendingType::UnsignedMax ||
			  InMode == EPCGExABBlendingType::AbsoluteMin ||
			  InMode == EPCGExABBlendingType::AbsoluteMax ||
			  InMode == EPCGExABBlendingType::Hash ||
			  InMode == EPCGExABBlendingType::UnsignedHash),
		  bConsiderOriginalValue(
			  InMode == EPCGExABBlendingType::Average ||
			  InMode == EPCGExABBlendingType::Add ||
			  InMode == EPCGExABBlendingType::Subtract ||
			  InMode == EPCGExABBlendingType::Weight ||
			  InMode == EPCGExABBlendingType::WeightedAdd ||
			  InMode == EPCGExABBlendingType::WeightedSubtract)
	{
	}

	// FBlendOperationFactory implementation

	TSharedPtr<IBlendOperation> FBlendOperationFactory::Create(
		EPCGMetadataTypes WorkingType,
		EPCGExABBlendingType BlendMode,
		bool bResetForMultiBlend,
		int32 InValueSize,
		int32 InValueAlignment)
	{
		switch (WorkingType)
		{
		case EPCGMetadataTypes::Boolean: return CreateTyped<bool>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Integer32: return CreateTyped<int32>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Integer64: return CreateTyped<int64>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Float: return CreateTyped<float>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Double: return CreateTyped<double>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Vector2: return CreateTyped<FVector2D>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Vector: return CreateTyped<FVector>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Vector4: return CreateTyped<FVector4>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Quaternion: return CreateTyped<FQuat>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Rotator: return CreateTyped<FRotator>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Transform: return CreateTyped<FTransform>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::String: return CreateTyped<FString>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::Name: return CreateTyped<FName>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::SoftObjectPath: return CreateTyped<FSoftObjectPath>(BlendMode, bResetForMultiBlend);
		case EPCGMetadataTypes::SoftClassPath: return CreateTyped<FSoftClassPath>(BlendMode, bResetForMultiBlend);
		default:
			if (InValueSize > 0)
			{
				return MakeShared<FCopyOnlyBlendOperation>(InValueSize, InValueAlignment, BlendMode, bResetForMultiBlend);
			}
			return nullptr;
		}
	}

	// FBlenderPool implementation

	TSharedPtr<IBlendOperation> FBlenderPool::Get(
		EPCGMetadataTypes WorkingType,
		EPCGExABBlendingType BlendMode,
		bool bResetForMultiBlend,
		int32 InValueSize,
		int32 InValueAlignment)
	{
		// Copy-only operations are size-specific per attribute — don't cache them
		if (InValueSize > 0 && WorkingType == EPCGMetadataTypes::Unknown)
		{
			return FBlendOperationFactory::Create(WorkingType, BlendMode, bResetForMultiBlend, InValueSize, InValueAlignment);
		}

		FKey Key{WorkingType, BlendMode, bResetForMultiBlend};

		{
			FScopeLock Lock(&CacheLock);
			if (TSharedPtr<IBlendOperation>* Found = Cache.Find(Key)) { return *Found; }
		}

		// Create new operation outside lock
		TSharedPtr<IBlendOperation> NewOp = FBlendOperationFactory::Create(WorkingType, BlendMode, bResetForMultiBlend, InValueSize, InValueAlignment);

		if (NewOp.IsValid())
		{
			FScopeLock Lock(&CacheLock);
			// Check again in case another thread added it
			if (TSharedPtr<IBlendOperation>* Found = Cache.Find(Key)) { return *Found; }
			Cache.Add(Key, NewOp);
		}

		return NewOp;
	}

	void FBlenderPool::Clear()
	{
		FScopeLock Lock(&CacheLock);
		Cache.Empty();
	}

	FBlenderPool& FBlenderPool::GetGlobal()
	{
		static FBlenderPool Instance;
		return Instance;
	}

	// Explicit template instantiations

	template class TBlendOperationImpl<bool>;
	template class TBlendOperationImpl<int32>;
	template class TBlendOperationImpl<int64>;
	template class TBlendOperationImpl<float>;
	template class TBlendOperationImpl<double>;
	template class TBlendOperationImpl<FVector2D>;
	template class TBlendOperationImpl<FVector>;
	template class TBlendOperationImpl<FVector4>;
	template class TBlendOperationImpl<FQuat>;
	template class TBlendOperationImpl<FRotator>;
	template class TBlendOperationImpl<FTransform>;
	template class TBlendOperationImpl<FString>;
	template class TBlendOperationImpl<FName>;
	template class TBlendOperationImpl<FSoftObjectPath>;
	template class TBlendOperationImpl<FSoftClassPath>;

	// Explicit instantiation of blend function getters
#define INST_BLEND_FUNC_GETTER(TYPE) \
	template FBlendFn BlendFunctions::GetBlendFunction<TYPE>(EPCGExABBlendingType); \
	template FFinalizeFn BlendFunctions::GetFinalizeFunction<TYPE>(EPCGExABBlendingType);

	INST_BLEND_FUNC_GETTER(bool)
	INST_BLEND_FUNC_GETTER(int32)
	INST_BLEND_FUNC_GETTER(int64)
	INST_BLEND_FUNC_GETTER(float)
	INST_BLEND_FUNC_GETTER(double)
	INST_BLEND_FUNC_GETTER(FVector2D)
	INST_BLEND_FUNC_GETTER(FVector)
	INST_BLEND_FUNC_GETTER(FVector4)
	INST_BLEND_FUNC_GETTER(FQuat)
	INST_BLEND_FUNC_GETTER(FRotator)
	INST_BLEND_FUNC_GETTER(FTransform)
	INST_BLEND_FUNC_GETTER(FString)
	INST_BLEND_FUNC_GETTER(FName)
	INST_BLEND_FUNC_GETTER(FSoftObjectPath)
	INST_BLEND_FUNC_GETTER(FSoftClassPath)

#undef INST_BLEND_FUNC_GETTER
}
