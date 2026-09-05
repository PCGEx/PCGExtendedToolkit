// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExCreateShapes.h"

#include "Core/PCGExShape.h"
#include "Core/PCGExShapeBuilderOperation.h"
#include "Core/PCGExShapeConfigBase.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Paths/PCGExPath.h"


#define LOCTEXT_NAMESPACE "PCGExCreateShapesElement"
#define PCGEX_NAMESPACE CreateShapes


PCGEX_INITIALIZE_ELEMENT(CreateShapes)
PCGEX_ELEMENT_BATCH_POINT_IMPL(CreateShapes)

bool FPCGExCreateShapesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExShapeProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(CreateShapes)

	if (Settings->bWriteShapeId)
	{
		PCGEX_VALIDATE_NAME(Settings->ShapeIdAttributeName)
	}

	return true;
}

bool FPCGExCreateShapesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCreateShapesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(CreateShapes)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to subdivide."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	if (Settings->OutputMode == EPCGExShapeOutputMode::PerSeed)
	{
		Context->MainBatch->Output();
	}
	else
	{
		Context->MainPoints->StageOutputs();
	}

	return Context->TryComplete();
}

namespace PCGExCreateShapes
{
	class FBuildShape final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FBuildShape)

		FBuildShape(const TSharedPtr<FPCGExShapeBuilderOperation>& InOperation, const TSharedRef<PCGExData::FFacade>& InShapeDataFacade, const TSharedPtr<PCGExShapes::FShape>& InShape)
			: FTask()
			  , ShapeDataFacade(InShapeDataFacade)
			  , Operation(InOperation)
			  , Shape(InShape)
		{
		}

		TSharedRef<PCGExData::FFacade> ShapeDataFacade;
		TSharedPtr<FPCGExShapeBuilderOperation> Operation;
		TSharedPtr<PCGExShapes::FShape> Shape;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			FPCGExCreateShapesContext* Context = TaskManager->GetContext<FPCGExCreateShapesContext>();
			PCGEX_SETTINGS(CreateShapes);

			BuildShape(Context, ShapeDataFacade, Operation, Shape);
		}
	};

	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExCreateShapes::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		Builders.Reserve(Context->BuilderFactories.Num());
		for (const TObjectPtr<const UPCGExShapeBuilderFactoryData>& Factory : Context->BuilderFactories)
		{
			TSharedPtr<FPCGExShapeBuilderOperation> Op = Factory->CreateOperation(Context);
			if (!Op->PrepareForSeeds(Context, PointDataFacade))
			{
				return false;
			}
			Builders.Add(Op);
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::CreateShapes::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			const PCGExData::FConstPoint PointRef = PointDataFacade->GetInPoint(Index);
			for (const TSharedPtr<FPCGExShapeBuilderOperation>& Op : Builders)
			{
				Op->PrepareShape(PointRef);
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		switch (Settings->OutputMode)
		{
		case EPCGExShapeOutputMode::PerDataset:
			OutputPerDataSet();
			break;
		case EPCGExShapeOutputMode::PerSeed:
			OutputPerSeed();
			break;
		case EPCGExShapeOutputMode::PerShape:
			OutputPerShape();
			break;
		}
	}

	void FProcessor::CompleteWork()
	{
		if (Settings->OutputMode == EPCGExShapeOutputMode::PerDataset)
		{
			PointDataFacade->WriteFastest(TaskManager);
		}
		else
		{
			for (const TSharedPtr<PCGExData::FFacade>& Facade : PerSeedFacades)
			{
				Facade->WriteFastest(TaskManager);
			}
		}
	}

	bool FProcessor::IsShapeValid(const TSharedPtr<PCGExShapes::FShape>& Shape) const
	{
		if (!Shape->IsValid())
		{
			return false;
		}
		if (Settings->bRemoveBelow && Shape->NumPoints < Settings->MinPointCount)
		{
			return false;
		}
		if (Settings->bRemoveAbove && Shape->NumPoints > Settings->MaxPointCount)
		{
			return false;
		}
		return true;
	}

	void FProcessor::OutputPerDataSet()
	{
		const int32 NumBuilders = Builders.Num();
		const int32 NumSeeds = PointDataFacade->GetNum();

		PCGEX_INIT_IO_VOID(PointDataFacade->Source, PCGExData::EIOInit::New)

		int32 StartIndex = 0;
		int32 NumPoints = 0;

		for (int i = 0; i < NumSeeds; i++)
		{
			for (int j = 0; j < NumBuilders; j++)
			{
				TSharedPtr<PCGExShapes::FShape> Shape = Builders[j]->Shapes[i];

				if (!IsShapeValid(Shape))
				{
					continue;
				}

				Shape->StartIndex = StartIndex;
				StartIndex += Shape->NumPoints;
				NumPoints += Shape->NumPoints;
			}
		}

		UPCGBasePointData* OutPointData = PointDataFacade->GetOut();
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPointData, NumPoints);

		for (int i = 0; i < NumSeeds; i++)
		{
			for (int j = 0; j < NumBuilders; j++)
			{
				TSharedPtr<PCGExShapes::FShape> Shape = Builders[j]->Shapes[i];
				if (!IsShapeValid(Shape))
				{
					continue;
				}
				PCGEX_LAUNCH(FBuildShape, Builders[j], PointDataFacade, Shape)
			}
		}
	}

	void FProcessor::OutputPerSeed()
	{
		const int32 NumBuilders = Builders.Num();
		const int32 NumSeeds = PointDataFacade->GetNum();

		const int32 NumOutputs = NumSeeds * NumBuilders;
		PerSeedFacades.Reserve(NumOutputs);

		for (int i = 0; i < NumSeeds; i++)
		{
			int32 StartIndex = 0;
			int32 NumPoints = 0;

			for (int j = 0; j < NumBuilders; j++)
			{
				TSharedPtr<PCGExShapes::FShape> Shape = Builders[j]->Shapes[i];

				if (!IsShapeValid(Shape))
				{
					continue;
				}

				Shape->StartIndex = StartIndex;
				StartIndex += Shape->NumPoints;
				NumPoints += Shape->NumPoints;
			}

			if (NumPoints <= 0)
			{
				continue;
			}

			TSharedPtr<PCGExData::FPointIO> IO = NewPointIO(PointDataFacade->Source, Settings->GetMainOutputPin(), i);
			PCGEX_INIT_IO_VOID(IO, PCGExData::EIOInit::New)

			PCGEX_MAKE_SHARED(IOFacade, PCGExData::FFacade, IO.ToSharedRef())
			PerSeedFacades.Add(IOFacade);

			PCGExPointArrayDataHelpers::SetNumPointsAllocated(IOFacade->GetOut(), NumPoints);

			for (int j = 0; j < NumBuilders; j++)
			{
				TSharedPtr<PCGExShapes::FShape> Shape = Builders[j]->Shapes[i];
				if (!IsShapeValid(Shape))
				{
					continue;
				}
				PCGEX_LAUNCH(FBuildShape, Builders[j], IOFacade.ToSharedRef(), Shape)
			}
		}
	}

	void FProcessor::OutputPerShape()
	{
		const int32 NumBuilders = Builders.Num();
		const int32 NumSeeds = PointDataFacade->GetNum();

		const int32 NumOutputs = NumSeeds * NumBuilders;
		PerSeedFacades.Reserve(NumOutputs);

		for (int j = 0; j < NumBuilders; j++)
		{
			if (Builders[j]->Shapes.IsEmpty())
			{
				continue;
			}

			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, BuildSeedShape)
			BuildSeedShape->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE, BuilderIndex = j](const int32 ShapeIndex, const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS

				const TSharedRef<FPCGExShapeBuilderOperation> Builder = This->Builders[BuilderIndex].ToSharedRef();
				const TSharedPtr<PCGExShapes::FShape> Shape = Builder->Shapes[ShapeIndex];

				if (!This->IsShapeValid(Shape))
				{
					return;
				}

				const TSharedPtr<PCGExData::FPointIO> IO = This->Context->MainPoints->Emplace_GetRef(This->PointDataFacade->Source, PCGExData::EIOInit::New);
				IO->IOIndex = BuilderIndex * 10000 + ShapeIndex;
				PCGEX_MAKE_SHARED(IOFacade, PCGExData::FFacade, IO.ToSharedRef())

				PCGExPointArrayDataHelpers::SetNumPointsAllocated(IOFacade->GetOut(), Shape->NumPoints);

				Shape->StartIndex = 0;
				BuildShape(This->Context, IOFacade.ToSharedRef(), Builder, Shape);

				IOFacade->WriteFastest(This->TaskManager);
			};

			BuildSeedShape->StartIterations(NumSeeds, 1);
		}
	}

	void BuildShape(FPCGExCreateShapesContext* Context, const TSharedRef<PCGExData::FFacade>& ShapeDataFacade, const TSharedPtr<FPCGExShapeBuilderOperation>& Operation, const TSharedPtr<PCGExShapes::FShape>& Shape)
	{
		PCGEX_SETTINGS(CreateShapes);

		UPCGBasePointData* ShapePoints = ShapeDataFacade->GetOut();

		ShapeDataFacade->Source->RepeatPoint(Shape->Seed.Index, Shape->StartIndex, Shape->NumPoints);
		TPCGValueRange<FVector> BoundsMin = ShapePoints->GetBoundsMinValueRange(false);
		TPCGValueRange<FVector> BoundsMax = ShapePoints->GetBoundsMaxValueRange(false);

		PCGExData::FScope SubScope = ShapeDataFacade->Source->GetOutScope(Shape->StartIndex, Shape->NumPoints);
		PCGEX_SUBSCOPE_LOOP(Index)
		{
			BoundsMin[Index] = Shape->Extents * -1;
			BoundsMax[Index] = Shape->Extents;
		}

		Operation->BuildShape(Shape, ShapeDataFacade, SubScope, ShapePoints->GetNumPoints() == SubScope.Count);

		if (Settings->bWriteShapeId)
		{
			FPCGAttributeIdentifier Identifier = PCGExMetaHelpers::GetAttributeIdentifier(Settings->ShapeIdAttributeName);

			if (Settings->OutputMode == EPCGExShapeOutputMode::PerShape && !Settings->bForceOutputToElement)
			{
				Identifier.MetadataDomain = EPCGMetadataDomainFlag::Data;
				const TSharedPtr<PCGExData::TBuffer<int32>> ShapeIdBuffer = ShapeDataFacade->GetWritable<int32>(Identifier, Operation->BaseConfig.ShapeId, true, PCGExData::EBufferInit::New);
			}
			else
			{
				Identifier.MetadataDomain = EPCGMetadataDomainFlag::Elements;
				const TSharedPtr<PCGExData::TBuffer<int32>> ShapeIdBuffer = ShapeDataFacade->GetWritable<int32>(Identifier, PCGExData::EBufferInit::New);
				const int32 MaxIndex = Shape->StartIndex + Shape->NumPoints;
				for (int i = Shape->StartIndex; i < MaxIndex; i++)
				{
					ShapeIdBuffer->SetValue(i, Operation->BaseConfig.ShapeId);
				}
			}
		}

		// Transform points

		const FTransform& TRA = Operation->Transform;
		FTransform TRB = Shape->Seed.GetTransform();

		TRB.SetScale3D(FVector::OneVector);

		TPCGValueRange<FTransform> OutTransforms = SubScope.Data->GetTransformValueRange(false);
		TPCGValueRange<int32> OutSeeds = SubScope.Data->GetSeedValueRange(false);

		PCGEX_SUBSCOPE_LOOP(PointIndex)
		{
			OutTransforms[PointIndex] = (OutTransforms[PointIndex] * TRB) * TRA;
			OutTransforms[PointIndex].SetScale3D(FVector::OneVector);
			OutSeeds[PointIndex] = PCGExRandomHelpers::ComputeSpatialSeed(OutTransforms[PointIndex].GetLocation(), TRB.GetLocation());
		}
	}

	void FProcessor::Output()
	{
		for (const TSharedPtr<PCGExData::FFacade>& IO : PerSeedFacades)
		{
			(void)IO->Source->StageOutput(Context);
		}
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
