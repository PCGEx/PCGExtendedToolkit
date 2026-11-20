// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathShift.h"


#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExShiftPathElement"
#define PCGEX_NAMESPACE ShiftPath

UPCGExShiftPathSettings::UPCGExShiftPathSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSupportClosedLoops = false;
}

PCGEX_INITIALIZE_ELEMENT(ShiftPath)

PCGExData::EIOInit UPCGExShiftPathSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(ShiftPath)

bool FPCGExShiftPathElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ShiftPath)

	if (Settings->ShiftType != EPCGExShiftType::CherryPick)
	{
		return true;
	}

	Context->ShiftedProperties = PCGExPointArrayDataHelpers::GetPointNativeProperties(Settings->CherryPickedProperties);

	TSet<FName> UniqueNames;

	UniqueNames.Reserve(Settings->CherryPickedAttributes.Num());
	Context->ShiftedAttributes.Reserve(Settings->CherryPickedAttributes.Num());

	for (FName Property : Settings->CherryPickedAttributes)
	{
		bool bAlreadyInSet = false;
		UniqueNames.Add(Property, &bAlreadyInSet);

		if (bAlreadyInSet)
		{
			continue;
		}

		FPCGAttributePropertyInputSelector Selector;
		Selector.Update(Property.ToString());

		if (Selector.GetSelection() != EPCGAttributePropertySelection::Attribute)
		{
			continue;
		}

		FPCGAttributeIdentifier& Identifier = Context->ShiftedAttributes.Add_GetRef(Selector.GetAttributeName());
		Identifier.MetadataDomain = PCGMetadataDomainID::Elements;
	}

	if (!Context->ShiftedAttributes.IsEmpty() && EnumHasAnyFlags(Context->ShiftedProperties, EPCGPointNativeProperties::MetadataEntry))
	{
		if (!Settings->bQuietDoubleShiftWarning)
		{
			PCGE_LOG(Warning, GraphAndLog, FTEXT("Shifting both attributes AND metadata entry property will result in a double shift of attributes. If that's intended, you can silence this warning in the settings."));
		}
	}

	return true;
}


bool FPCGExShiftPathElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExShiftPathElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ShiftPath)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and won't be processed."))

		// TODO : Skip completion

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bPrefetchData = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to shift."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainBatch->Output();
	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExShiftPath
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExShiftPath::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		MaxIndex = PointDataFacade->GetNum(PCGExData::EIOSide::In) - 1;
		PivotIndex = Settings->bReverseShift ? MaxIndex : 0;

		if (Settings->InputMode == EPCGExShiftPathMode::Relative)
		{
			PivotIndex = PCGExMath::TruncateDbl(static_cast<double>(MaxIndex) * static_cast<double>(Settings->RelativeConstant), Settings->Truncate);
		}
		else if (Settings->InputMode == EPCGExShiftPathMode::Discrete)
		{
			PivotIndex = Settings->DiscreteConstant;
		}
		else if (Settings->InputMode == EPCGExShiftPathMode::Filter)
		{
			if (Context->FilterFactories.IsEmpty())
			{
				return false;
			}

			PCGEX_ASYNC_GROUP_CHKD(TaskManager, FilterTask)

			FilterTask->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS

				if (This->Settings->bReverseShift)
				{
					for (int i = This->MaxIndex; i >= 0; i--)
					{
						if (This->PointFilterCache[i])
						{
							This->PivotIndex = i;
							return;
						}
					}
				}
				else
				{
					for (int i = 0; i <= This->MaxIndex; i++)
					{
						if (This->PointFilterCache[i])
						{
							This->PivotIndex = i;
							return;
						}
					}
				}
			};

			FilterTask->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS
				This->ProcessPoints(Scope);
			};

			FilterTask->StartSubLoops(PointDataFacade->GetNum(), PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());
			return true;
		}

		if (Settings->bReverseShift)
		{
			PivotIndex = MaxIndex - PivotIndex;
		}
		PivotIndex = PCGExMath::SanitizeIndex(PivotIndex, MaxIndex, Settings->IndexSafety);

		if (!FMath::IsWithinInclusive(PivotIndex, 0, MaxIndex))
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FText::FromString(TEXT("Some data has invalid pivot index.")));
		}

		if (Settings->ShiftType == EPCGExShiftType::CherryPick && !Context->ShiftedAttributes.IsEmpty())
		{
			Buffers.Init(nullptr, Context->ShiftedAttributes.Num());

			PCGEX_ASYNC_GROUP_CHKD(TaskManager, InitBuffers)

			InitBuffers->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS
				const FPCGMetadataAttributeBase* Attr = This->PointDataFacade->FindConstAttribute(This->GetContext()->ShiftedAttributes[Index]);

				if (!Attr)
				{
					return;
				}

				TSharedPtr<PCGExData::IBuffer> Buffer = This->PointDataFacade->GetWritable(static_cast<EPCGMetadataTypes>(Attr->GetTypeId()), Attr, PCGExData::EBufferInit::Inherit);
				This->Buffers[Index] = Buffer;
			};

			InitBuffers->StartIterations(Context->ShiftedAttributes.Num(), 1);
		}

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::ShiftPath::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);
	}

	void FProcessor::CompleteWork()
	{
		if (PivotIndex == 0 || PivotIndex == MaxIndex)
		{
			return;
		}

		if (!FMath::IsWithinInclusive(PivotIndex, 0, MaxIndex))
		{
			bIsProcessorValid = false;
			return;
		}

		PCGExArrayHelpers::ArrayOfIndices(Indices, PointDataFacade->GetIn()->GetNumPoints());

		if (Settings->bReverseShift)
		{
			PCGExMath::ReverseRange(Indices, 0, PivotIndex);
			PCGExMath::ReverseRange(Indices, PivotIndex + 1, MaxIndex);
		}
		else
		{
			PCGExMath::ReverseRange(Indices, 0, PivotIndex - 1);
			PCGExMath::ReverseRange(Indices, PivotIndex, MaxIndex);
		}

		Algo::Reverse(Indices);

		if (Settings->ShiftType == EPCGExShiftType::Index)
		{
			return;
		}

		if (Settings->ShiftType == EPCGExShiftType::Metadata)
		{
			PointDataFacade->Source->InheritProperties(Indices, EPCGPointNativeProperties::MetadataEntry);
		}
		else if (Settings->ShiftType == EPCGExShiftType::Properties)
		{
			PointDataFacade->Source->InheritProperties(Indices, PointDataFacade->GetAllocations() & ~EPCGPointNativeProperties::MetadataEntry);
		}
		else if (Settings->ShiftType == EPCGExShiftType::MetadataAndProperties)
		{
			PointDataFacade->Source->InheritProperties(Indices, PointDataFacade->GetAllocations());
		}
		else if (Settings->ShiftType == EPCGExShiftType::CherryPick)
		{
			if (Context->ShiftedProperties != EPCGPointNativeProperties::None)
			{
				PointDataFacade->Source->InheritProperties(Indices, Context->ShiftedProperties);
			}

			if (!Buffers.IsEmpty())
			{
				PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, InitBuffers)

				InitBuffers->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
				{
					PCGEX_ASYNC_THIS
					This->PointDataFacade->WriteFastest(This->TaskManager);
				};

				InitBuffers->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
				{
					PCGEX_ASYNC_THIS
					TSharedPtr<PCGExData::IBuffer> Buffer = This->Buffers[Index];

					if (!Buffer || Buffer->GetUnderlyingDomain() != PCGExData::EDomainType::Elements)
					{
						return;
					}

					// Container/extended-typed buffers report GetTypeId()=Unknown (FPropertyBuffer fallback),
					// so the typed dispatch below safely no-ops for them -- those buffers retain their
					// pre-shift order. Property-aware reorder (scratch byte array + per-element CopyCompleteValue)
					// is a future addition if path-shift on container attributes becomes a real use case.
					PCGExMetaHelpers::ExecuteWithRightType(Buffer->GetTypeId(), [&](auto DummyValue)
					{
						using T = decltype(DummyValue);
						TSharedPtr<PCGExData::TArrayBuffer<T>> TypedBuffer = StaticCastSharedPtr<PCGExData::TArrayBuffer<T>>(This->Buffers[Index]);

						if (!TypedBuffer)
						{
							return;
						}

						TArray<T>& Values = *TypedBuffer->GetOutValues().Get();
						PCGExArrayHelpers::ReorderArray(Values, This->Indices);
					});
				};

				InitBuffers->StartIterations(Buffers.Num(), 1);
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
