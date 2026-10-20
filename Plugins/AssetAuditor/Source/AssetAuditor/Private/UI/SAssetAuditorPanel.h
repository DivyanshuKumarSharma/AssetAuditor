#pragma once

#include "CoreMinimal.h"
#include "AssetAuditTypes.h"
#include "EditorUndoClient.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"

/** SListView items must be shared pointers: the view and its rows hold references to them. */
using FAssetAuditIssuePtr = TSharedPtr<FAssetAuditIssue>;

/**
 * Contents of the Asset Auditor tab: toolbar, filters and the results table.
 * Also an undo client, so Ctrl+Z on one of our fixes refreshes the table instead of leaving it stale.
 * The self-registering variant registers with GEditor on construction and unregisters on destruction.
 */
class SAssetAuditorPanel : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SAssetAuditorPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static const FName ColumnSeverity;
	static const FName ColumnRule;
	static const FName ColumnAsset;
	static const FName ColumnMessage;
	static const FName ColumnFix;

	/** Entry point for the per-row Fix button, the toolbar and the context menu. */
	void FixIssues(const TArray<FAssetAuditIssuePtr>& Issues);

	// FEditorUndoClient
	virtual bool MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const override;
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

private:
	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SHeaderRow> BuildHeaderRow();
	TSharedRef<ITableRow> OnGenerateRow(FAssetAuditIssuePtr Issue, const TSharedRef<STableViewBase>& OwnerTable);
	void OnRowDoubleClicked(FAssetAuditIssuePtr Issue);
	TSharedPtr<SWidget> OnContextMenuOpening();

	// Scanning
	bool CanScan() const;
	void RunScan();

	// Filtering and sorting: AllIssues is the scan result, VisibleIssues is what the list shows.
	void RefreshVisibleIssues();
	bool PassesFilter(const FAssetAuditIssue& Issue) const;
	void OnSortModeChanged(EColumnSortPriority::Type Priority, const FName& Column, EColumnSortMode::Type NewMode);
	EColumnSortMode::Type GetSortMode(FName Column) const;

	// Actions
	TArray<FAssetData> GetSelectedAssets() const;
	TArray<FAssetAuditIssuePtr> GetSelectedFixableIssues() const;
	void BrowseToSelected() const;
	void OpenSelected() const;
	void ExportCsv() const;

	FText GetSummaryText() const;

	TArray<FAssetAuditIssuePtr> AllIssues;
	TArray<FAssetAuditIssuePtr> VisibleIssues;
	TSharedPtr<SListView<FAssetAuditIssuePtr>> ListView;

	FString ScanPath = TEXT("/Game");
	FString SearchText;
	bool bHasScanned = false;

	/** Indexed by EAssetAuditSeverity. */
	bool SeverityVisible[3] = { true, true, true };

	FName SortColumn = ColumnSeverity;
	EColumnSortMode::Type SortMode = EColumnSortMode::Descending;
};
