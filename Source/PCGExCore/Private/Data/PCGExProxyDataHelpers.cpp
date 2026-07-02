// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/PCGExProxyDataHelpers.h"

#include "Data/PCGExData.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExProxyData.h"
#include "Data/PCGExProxyDataImpl.h"
#include "Data/PCGExSubAccessor.h"

namespace PCGExData
{
	template <typename T_REAL>
	void TryGetInOutAttr(
		const FProxyDescriptor& InDescriptor,
		const TSharedPtr<FFacade>& InDataFacade,
		const FPCGMetadataAttributeBase*& OutInAttribute,
		FPCGMetadataAttributeBase*& OutOutAttribute)
	{
		OutInAttribute = nullptr;
		OutOutAttribute = nullptr;

		if (InDescriptor.Role == EProxyRole::Read)
		{
			if (InDescriptor.Side == EIOSide::In)
			{
				OutInAttribute = PCGExMetaHelpers::TryGetConstAttribute<T_REAL>(
					InDataFacade->GetIn(),
					PCGExMetaHelpers::GetAttributeIdentifier(InDescriptor.Selector, InDataFacade->GetIn()));
			}
			else
			{
				OutInAttribute = PCGExMetaHelpers::TryGetConstAttribute<T_REAL>(
					InDataFacade->GetOut(),
					PCGExMetaHelpers::GetAttributeIdentifier(InDescriptor.Selector, InDataFacade->GetIn()));
			}

			if (OutInAttribute)
			{
				OutOutAttribute = const_cast<FPCGMetadataAttributeBase*>(OutInAttribute);
			}

			check(OutInAttribute);
		}
		else if (InDescriptor.Role == EProxyRole::Write)
		{
			OutOutAttribute = InDataFacade->Source->FindOrCreateAttribute(
				PCGExMetaHelpers::GetAttributeIdentifier(InDescriptor.Selector, InDataFacade->GetOut()),
				T_REAL{});
			if (OutOutAttribute)
			{
				OutInAttribute = OutOutAttribute;
			}

			check(OutOutAttribute);
		}
	}

	// Acquires or creates a typed buffer for a given descriptor.
	// Reading from Output side has special fallback logic: if no readable output buffer exists,
	// it creates a writable buffer initialized from input (Inherit) and marks it readable,
	// enabling read-modify-write patterns on output attributes.
	template <typename T_REAL>
	TSharedPtr<TBuffer<T_REAL>> TryGetBuffer(
		FPCGExContext* InContext,
		const FProxyDescriptor& InDescriptor,
		const TSharedPtr<FFacade>& InDataFacade)
	{
		const FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::GetAttributeIdentifier(
			InDescriptor.Selector,
			InDescriptor.Side == EIOSide::In ? InDataFacade->GetIn() : InDataFacade->GetOut());

		// Check for existing buffer
		TSharedPtr<TBuffer<T_REAL>> ExistingBuffer = InDataFacade->FindBuffer<T_REAL>(Identifier);
		TSharedPtr<TBuffer<T_REAL>> Buffer;

		if (InDescriptor.Role == EProxyRole::Read)
		{
			if (InDescriptor.Side == EIOSide::In)
			{
				if (ExistingBuffer && ExistingBuffer->IsReadable())
				{
					Buffer = ExistingBuffer;
				}

				if (!Buffer)
				{
					Buffer = InDataFacade->GetReadable<T_REAL>(Identifier, EIOSide::In, true);
				}
			}
			else if (InDescriptor.Side == EIOSide::Out)
			{
				if (ExistingBuffer)
				{
					if (ExistingBuffer->ReadsFromOutput())
					{
						Buffer = ExistingBuffer;
					}

					if (!Buffer)
					{
						if (ExistingBuffer->IsWritable())
						{
							Buffer = InDataFacade->GetReadable<T_REAL>(Identifier, EIOSide::Out, true);
						}

						if (!Buffer)
						{
							PCGE_LOG_C(Error, GraphAndLog, InContext,
							           FTEXT("Trying to read from an output buffer that doesn't exist yet."));
							return nullptr;
						}
					}
				}
				else
				{
					Buffer = InDataFacade->GetWritable<T_REAL>(Identifier, T_REAL{}, true, EBufferInit::Inherit);
					if (Buffer)
					{
						Buffer->EnsureReadable();
					}
					else
					{
						PCGE_LOG_C(Error, GraphAndLog, InContext,
						           FTEXT("Could not create read/write buffer."));
						return nullptr;
					}
				}
			}
		}
		else if (InDescriptor.Role == EProxyRole::Write)
		{
			Buffer = InDataFacade->GetWritable<T_REAL>(Identifier, T_REAL{}, true, EBufferInit::Inherit);
		}

		return Buffer;
	}

#pragma region externalization TryGetInOutAttr / TryGetBuffer

#define PCGEX_TPL(_TYPE, _NAME, ...) \
	template PCGEXCORE_API void TryGetInOutAttr<_TYPE>( \
		const FProxyDescriptor& InDescriptor, \
		const TSharedPtr<FFacade>& InDataFacade, \
		const FPCGMetadataAttributeBase*& OutInAttribute, \
		FPCGMetadataAttributeBase*& OutOutAttribute); \
	template PCGEXCORE_API TSharedPtr<TBuffer<_TYPE>> TryGetBuffer<_TYPE>( \
		FPCGExContext* InContext, \
		const FProxyDescriptor& InDescriptor, \
		const TSharedPtr<FFacade>& InDataFacade);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

#pragma endregion

	//
	// Internal proxy creation helpers
	//
	namespace Internal
	{
		template <typename T_REAL>
		TSharedPtr<IBufferProxy> CreateRawProxy(
			FPCGExContext* InContext,
			const FProxyDescriptor& InDescriptor,
			const TSharedPtr<FFacade>& InDataFacade)
		{
			TSharedPtr<TArray<T_REAL>> Buffer = MakeShared<TArray<T_REAL>>();

			if (InDescriptor.Role == EProxyRole::Read)
			{
				Buffer->Init(T_REAL{}, InDataFacade->GetNum(EIOSide::In));
			}
			else
			{
				Buffer->Init(T_REAL{}, InDataFacade->GetNum(EIOSide::Out));
			}

			auto Proxy = MakeShared<TRawBufferProxy<T_REAL>>(InDescriptor.WorkingType);
			Proxy->Buffer = Buffer;
			return Proxy;
		}

		template <typename T_REAL>
		TSharedPtr<IBufferProxy> CreateAttributeProxy(
			FPCGExContext* InContext,
			const FProxyDescriptor& InDescriptor,
			const TSharedPtr<FFacade>& InDataFacade)
		{
			TSharedPtr<TBuffer<T_REAL>> Buffer = TryGetBuffer<T_REAL>(InContext, InDescriptor, InDataFacade);

			if (!Buffer)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Failed to initialize proxy buffer."));
				return nullptr;
			}

			auto Proxy = MakeShared<TAttributeBufferProxy<T_REAL>>(InDescriptor.WorkingType);
			Proxy->Buffer = Buffer;
			return Proxy;
		}

		template <typename T_REAL>
		TSharedPtr<IBufferProxy> CreateDirectProxy(
			const FProxyDescriptor& InDescriptor,
			const TSharedPtr<FFacade>& InDataFacade,
			bool bIsDataDomain)
		{
			const FPCGMetadataAttributeBase* InAttribute = nullptr;
			FPCGMetadataAttributeBase* OutAttribute = nullptr;

			TryGetInOutAttr<T_REAL>(InDescriptor, InDataFacade, InAttribute, OutAttribute);

			if (bIsDataDomain)
			{
				auto Proxy = MakeShared<TDirectDataAttributeProxy<T_REAL>>(InDescriptor.WorkingType);
				Proxy->InAttribute = InAttribute;
				Proxy->OutAttribute = OutAttribute;
				return Proxy;
			}
			auto Proxy = MakeShared<TDirectAttributeProxy<T_REAL>>(InDescriptor.WorkingType);
			Proxy->InAttribute = InAttribute;
			Proxy->OutAttribute = OutAttribute;
			return Proxy;
		}

		template <typename T_CONST>
		TSharedPtr<IBufferProxy> CreateConstantProxyFromProperty(
			const FProxyDescriptor& InDescriptor,
			UPCGBasePointData* PointData)
		{
			auto Proxy = MakeShared<TConstantProxy<T_CONST>>(InDescriptor.WorkingType);

			if (!PointData->IsEmpty())
			{
				const FConstPoint Point(PointData, 0);

				switch (InDescriptor.Selector.GetPointProperty())
				{
				case EPCGPointProperties::Density:
					Proxy->SetConstant(Point.GetDensity());
					break;
				case EPCGPointProperties::BoundsMin:
					Proxy->SetConstant(Point.GetBoundsMin());
					break;
				case EPCGPointProperties::BoundsMax:
					Proxy->SetConstant(Point.GetBoundsMax());
					break;
				case EPCGPointProperties::Extents:
					Proxy->SetConstant(Point.GetExtents());
					break;
				case EPCGPointProperties::Color:
					Proxy->SetConstant(Point.GetColor());
					break;
				case EPCGPointProperties::Position:
					Proxy->SetConstant(Point.GetLocation());
					break;
				case EPCGPointProperties::Rotation:
					Proxy->SetConstant(Point.GetRotation());
					break;
				case EPCGPointProperties::Scale:
					Proxy->SetConstant(Point.GetScale3D());
					break;
				case EPCGPointProperties::Transform:
					Proxy->SetConstant(Point.GetTransform());
					break;
				case EPCGPointProperties::Steepness:
					Proxy->SetConstant(Point.GetSteepness());
					break;
				case EPCGPointProperties::LocalCenter:
					Proxy->SetConstant(Point.GetLocalCenter());
					break;
				case EPCGPointProperties::Seed:
					Proxy->SetConstant(Point.GetSeed());
					break;
				default:
					Proxy->SetConstant(T_CONST{});
					break;
				}
			}
			else
			{
				Proxy->SetConstant(T_CONST{});
			}

			return Proxy;
		}
	}

	template <typename T>
	TSharedPtr<IBufferProxy> GetConstantProxyBuffer(const T& Constant, EPCGMetadataTypes InWorkingType)
	{
		auto Proxy = MakeShared<TConstantProxy<T>>(InWorkingType);
		Proxy->SetConstant(Constant);
		return Proxy;
	}

#pragma region externalization GetConstantProxyBuffer

#define PCGEX_TPL(_TYPE, _NAME, ...) \
template PCGEXCORE_API TSharedPtr<IBufferProxy> GetConstantProxyBuffer<_TYPE>(const _TYPE& Constant, EPCGMetadataTypes InWorkingType);
	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	// Central factory for creating buffer proxies from a descriptor.
	// Dispatch order: Raw → Constant → Attribute (Direct or Buffered) → Property → Extra.
	// When the Shared flag is set, creation routes through the pool's atomic GetOrCreate
	// so the side-effectful InitForRole (e.g. PointData->AllocateProperties) runs at most
	// once per descriptor -- preventing concurrent UObject mutation from parallel callers.
	TSharedPtr<IBufferProxy> GetProxyBuffer(FPCGExContext* InContext, const FProxyDescriptor& InDescriptor)
	{
		const TSharedPtr<FFacade> InDataFacade = InDescriptor.DataFacade.Pin();
		UPCGBasePointData* PointData = nullptr;

		// Determine point data source
		if (!InDataFacade)
		{
			PointData = const_cast<UPCGBasePointData*>(InDescriptor.PointData);

			if (PointData && InDescriptor.Selector.GetSelection() == EPCGAttributePropertySelection::Property)
			{
				// Property-only access without facade - OK
			}
			else
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Proxy descriptor has no valid source."));
				return nullptr;
			}
		}
		else
		{
			if (InDescriptor.HasFlag(EProxyFlags::Constant) || InDescriptor.Side == EIOSide::In)
			{
				PointData = const_cast<UPCGBasePointData*>(InDataFacade->GetIn());
			}
			else
			{
				PointData = InDataFacade->GetOut();
			}

			if (!PointData)
			{
				PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Proxy descriptor attempted to work with a null PointData."));
				return nullptr;
			}
		}

		auto Factory = [&]() -> TSharedPtr<IBufferProxy>
		{
			TSharedPtr<IBufferProxy> OutProxy = nullptr;

			// Handle raw proxy
			if (InDescriptor.HasFlag(EProxyFlags::Raw))
			{
				PCGExMetaHelpers::ExecuteWithRightType(InDescriptor.RealType, [&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					OutProxy = Internal::CreateRawProxy<T>(InContext, InDescriptor, InDataFacade);
				});

				if (OutProxy)
				{
					OutProxy->SetSubSelection(InDescriptor.SubSelection, InDescriptor.SourceDesc.IsValid() ? &InDescriptor.SourceDesc : nullptr);
				}
				return OutProxy;
			}

			// Handle constant proxy
			if (InDescriptor.HasFlag(EProxyFlags::Constant))
			{
				const PCGMetadataEntryKey Key =
					InDataFacade->GetIn()->IsEmpty()
					? PCGInvalidEntryKey
					: InDataFacade->GetIn()->GetMetadataEntry(0);

				PCGExMetaHelpers::ExecuteWithRightType(InDescriptor.RealType, [&](auto DummyValue)
				{
					using T = decltype(DummyValue);

					if (InDescriptor.Selector.GetSelection() == EPCGAttributePropertySelection::Attribute)
					{
						if (const FPCGMetadataAttributeBase* Attr = PCGExMetaHelpers::TryGetConstAttribute<T>(InDataFacade->GetIn(), PCGExMetaHelpers::GetAttributeIdentifier(InDescriptor.Selector, InDataFacade->GetIn())))
						{
							const bool bIsData = Attr->GetMetadataDomain()->GetDomainID().Flag == EPCGMetadataDomainFlag::Data;
							OutProxy = GetConstantProxyBuffer<T>(bIsData ? Helpers::ReadDataValue<T>(Attr) : Attr->GetValueFromItemKey<T>(Key), InDescriptor.WorkingType);
						}
					}
					else if (InDescriptor.Selector.GetSelection() == EPCGAttributePropertySelection::Property)
					{
						OutProxy = Internal::CreateConstantProxyFromProperty<T>(InDescriptor, PointData);
					}
				});

				if (OutProxy)
				{
					OutProxy->SetSubSelection(InDescriptor.SubSelection, InDescriptor.SourceDesc.IsValid() ? &InDescriptor.SourceDesc : nullptr);
				}
				return OutProxy;
			}

			// Handle attribute proxy
			if (InDescriptor.Selector.GetSelection() == EPCGAttributePropertySelection::Attribute)
			{
				// Container attributes (TArray/TSet/TMap) must route
				// through FPropertyBuffer, not TBuffer<T>. The Desc-based
				// RealType is the inner element type (e.g. Vector for
				// TArray<FVector>), so the normal ExecuteWithRightType path
				// would instantiate TBuffer<FVector> -- wrong for container
				// storage (which is FScriptArray layout). Skip straight to
				// the Tier 3 FPropertyBuffer fallback for containers.
				const bool bIsContainer = InDescriptor.SourceDesc.IsValid() && !InDescriptor.SourceDesc.IsSingleValue();

				if (!bIsContainer && InDescriptor.HasFlag(EProxyFlags::Direct))
				{
					// Direct attribute access (scalar only)
					const FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::GetAttributeIdentifier(InDescriptor.Selector, InDataFacade->GetIn());
					const FPCGMetadataAttributeBase* BaseAttr = InDataFacade->FindConstAttribute(Identifier, InDescriptor.Side);
					const bool bIsDataDomain = BaseAttr && BaseAttr->GetMetadataDomain()->GetDomainID().Flag == EPCGMetadataDomainFlag::Data;

					PCGExMetaHelpers::ExecuteWithRightType(InDescriptor.RealType, [&](auto DummyValue)
					{
						using T = decltype(DummyValue);
						OutProxy = Internal::CreateDirectProxy<T>(InDescriptor, InDataFacade, bIsDataDomain);
					});
				}
				else if (!bIsContainer)
				{
					// Buffered attribute access (scalar only)
					PCGExMetaHelpers::ExecuteWithRightType(InDescriptor.RealType, [&](auto DummyValue)
					{
						using T = decltype(DummyValue);
						OutProxy = Internal::CreateAttributeProxy<T>(InContext, InDescriptor, InDataFacade);
					});
				}

				// Tier 3 fallback: wrap FPropertyBuffer via void* R/W for types not in
				// PCGEX_FOREACH_SUPPORTEDTYPES. FFacade::GetWritable/GetDefaultReadable gate on
				// InitProperty internally -- a failed size derivation returns nullptr.
				if (!OutProxy)
				{
					const FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::GetAttributeIdentifier(
						InDescriptor.Selector,
						InDescriptor.Side == EIOSide::In ? InDataFacade->GetIn() : InDataFacade->GetOut());

					TSharedPtr<IBuffer> PropertyBuf;

					if (InDescriptor.Role == EProxyRole::Read)
					{
						PropertyBuf = InDataFacade->GetDefaultReadable(Identifier, InDescriptor.Side);
					}
					else
					{
						// For writes, find the source attribute and use the type-erased writable path
						const FPCGMetadataAttributeBase* SrcAttr = InDataFacade->FindConstAttribute(Identifier, EIOSide::In);
						if (!SrcAttr)
						{
							SrcAttr = InDataFacade->FindConstAttribute(Identifier, EIOSide::Out);
						}

						// Output Mode: New -- create the attribute from the template Desc
						// the factory propagated (mirrors FPCGExProperty_Struct::InitializeOutput).
						if (!SrcAttr && InDescriptor.SourceDesc.IsValid())
						{
							if (UPCGBasePointData* OutData = InDataFacade->GetOut();
								OutData && OutData->Metadata)
							{
								FPCGMetadataAttributeDesc OutDesc = InDescriptor.SourceDesc;
								OutDesc.Name = Identifier.Name;
								SrcAttr = OutData->Metadata->CreateAttribute(
									Identifier, OutDesc,
									/*bAllowsInterp=*/true, /*bOverrideParent=*/true);
							}
						}

						if (SrcAttr)
						{
							PropertyBuf = InDataFacade->GetWritable(
								PCGExMetaHelpers::GetAttributeType(SrcAttr),
								SrcAttr, EBufferInit::Inherit);
						}
					}

					if (PropertyBuf)
					{
						OutProxy = MakeShared<FPropertyBufferProxy>(PropertyBuf, InDescriptor.RealType, InDescriptor.WorkingType);
					}
				}
			}
			// Handle point property proxy
			else if (InDescriptor.Selector.GetSelection() == EPCGAttributePropertySelection::Property)
			{
				OutProxy = MakeShared<FPointPropertyProxy>(InDescriptor.Selector.GetPointProperty(), InDescriptor.WorkingType);
			}
			// Handle extra property proxy
			else
			{
				OutProxy = MakeShared<FPointExtraPropertyProxy>(EPCGExtraProperties::Index, InDescriptor.WorkingType);
			}

			// Finalize proxy setup
			if (OutProxy)
			{
				OutProxy->Data = PointData;
				OutProxy->SetSubSelection(InDescriptor.SubSelection, InDescriptor.SourceDesc.IsValid() ? &InDescriptor.SourceDesc : nullptr);
				OutProxy->InitForRole(InDescriptor.Role);

				if (!OutProxy->Validate(InDescriptor))
				{
					PCGE_LOG_C(Error, GraphAndLog, InContext, FText::Format( FTEXT("Proxy buffer doesn't match desired types: \"{0}\""), FText::FromString(PCGExMetaHelpers::GetSelectorDisplayName(InDescriptor.Selector))));
					OutProxy = nullptr;
				}
			}

			return OutProxy;
		};

		if (InDescriptor.HasFlag(EProxyFlags::Shared))
		{
			// Route to the descriptor's facade-local pool so creation no longer funnels through a
			// single context-wide chokepoint: a per-cluster target facade's pool is uncontended,
			// and shared source facades distribute contention while retaining their read proxies.
			// Descriptors with no pinnable facade fall back to the context pool.
			
			// TODO BUG : Something really bad is happening here when using FUnionProcessors with union blender
			// Need to double-check data-to-element domain path and how it's possibly messed up by
			// multi-layered multi-source blending during fusing
			IBufferProxyPool& Pool = /* InDataFacade ? InDataFacade->GetProxyPool() : */ *InContext->BufferProxyPool;
			return Pool.GetOrCreate(InDescriptor, Factory);
		}
		return Factory();
	}

#pragma endregion

	bool GetPerFieldProxyBuffers(
		FPCGExContext* InContext,
		const FProxyDescriptor& InBaseDescriptor,
		const int32 NumDesiredFields,
		TArray<TSharedPtr<IBufferProxy>>& OutProxies)
	{
		OutProxies.Reset(NumDesiredFields);
		const int32 Dimensions = FMath::Min(4, GetNumFieldsForType(InBaseDescriptor.RealType));

		if (Dimensions == -1 &&
			(!InBaseDescriptor.SubSelection.HasSelection() || !InBaseDescriptor.SubSelection.IsComponentSelection()))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext,
			           FTEXT("Can't automatically break complex type into sub-components. "
				           "Use a narrower selector or a supported type."));
			return false;
		}

		const int32 MaxIndex = Dimensions == -1 ? 2 : Dimensions - 1;

		if (InBaseDescriptor.SubSelection.HasSelection())
		{
			if (InBaseDescriptor.SubSelection.IsFieldSelection())
			{
				// Single specific field - use same proxy for all
				const TSharedPtr<IBufferProxy> Proxy = GetProxyBuffer(InContext, InBaseDescriptor);
				if (!Proxy)
				{
					return false;
				}

				for (int i = 0; i < NumDesiredFields; i++)
				{
					OutProxies.Add(Proxy);
				}
				return true;
			}

			// Create individual field proxies
			for (int i = 0; i < NumDesiredFields; i++)
			{
				FProxyDescriptor SingleFieldCopy = InBaseDescriptor;
				SingleFieldCopy.SetFieldIndex(FMath::Clamp(i, 0, MaxIndex));

				const TSharedPtr<IBufferProxy> Proxy = GetProxyBuffer(InContext, SingleFieldCopy);
				if (!Proxy)
				{
					return false;
				}
				OutProxies.Add(Proxy);
			}
		}
		else
		{
			// No subselection - create field proxies
			for (int i = 0; i < NumDesiredFields; i++)
			{
				FProxyDescriptor SingleFieldCopy = InBaseDescriptor;
				SingleFieldCopy.SetFieldIndex(FMath::Clamp(i, 0, MaxIndex));

				const TSharedPtr<IBufferProxy> Proxy = GetProxyBuffer(InContext, SingleFieldCopy);
				if (!Proxy)
				{
					return false;
				}
				OutProxies.Add(Proxy);
			}
		}

		return true;
	}
}
