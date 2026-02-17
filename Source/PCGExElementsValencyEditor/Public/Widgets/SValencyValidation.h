// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UPCGExValencyCageEditorMode;

/**
 * Validation message with severity and source.
 */
struct FValencyValidationMessage
{
	enum class ESeverity : uint8
	{
		Info,
		Warning,
		Error
	};

	ESeverity Severity = ESeverity::Warning;
	TWeakObjectPtr<AActor> SourceActor;
	FString SourceName;
	FString Message;
};

/**
 * Validation panel showing compilation diagnostics and connection health warnings.
 * Messages are generated on demand by running validation checks against the scene.
 */
class SValencyValidation : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SValencyValidation) {}
		SLATE_ARGUMENT(UPCGExValencyCageEditorMode*, EditorMode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Run validation checks and update the message list */
	void RunValidation();

private:
	UPCGExValencyCageEditorMode* EditorMode = nullptr;

	/** Validation messages */
	TArray<TSharedPtr<FValencyValidationMessage>> Messages;

	/** The list view widget */
	TSharedPtr<SListView<TSharedPtr<FValencyValidationMessage>>> ListView;

	/** List view callbacks */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FValencyValidationMessage> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnMessageClicked(TSharedPtr<FValencyValidationMessage> Item, ESelectInfo::Type SelectInfo);

	/** Scene change delegate handle */
	FDelegateHandle OnSceneChangedHandle;

	/** Validation check methods */
	void ValidateCages();
	void ValidateVolumes();
	void ValidateScene();
};
