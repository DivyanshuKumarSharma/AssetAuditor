#include "UI/SAssetAuditorPanel.h"

#include "AssetAuditScanner.h"
#include "AssetAuditorSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "AssetAuditor"

const FName SAssetAuditorPanel::ColumnSeverity(TEXT("Severity"));
const FName SAssetAuditorPanel::ColumnRule(TEXT("Rule"));
const FName SAssetAuditorPanel::ColumnAsset(TEXT("Asset"));
const FName SAssetAuditorPanel::ColumnMessage(TEXT("Message"));
const FName SAssetAuditorPanel::ColumnFix(TEXT("Fix"));

namespace
{
	IAssetRegistry& GetAssetRegistry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	}

	const FSlateBrush* GetSeverityBrush(EAssetAuditSeverity Severity)
	{
		switch (Severity)
		{
		case EAssetAuditSeverity::Error:   return FAppStyle::GetBrush("Icons.ErrorWithColor");
		case EAssetAuditSeverity::Warning: return FAppStyle::GetBrush("Icons.WarningWithColor");
		default:                           return FAppStyle::GetBrush("Icons.InfoWithColor");
		}
	}

	void BrowseTo(const TArray<FAssetData>& Assets)
	{
		if (Assets.Num() > 0)
		{
			FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			ContentBrowser.Get().SyncBrowserToAssets(Assets);
		}
	}

	/**
	 * One row of the table. SMultiColumnTableRow asks for one widget per visible column,
	 * so hiding or reordering columns in the header needs no extra code here.
	 */
	class SAssetAuditIssueRow : public SMultiColumnTableRow<FAssetAuditIssuePtr>
	{
	public:
		SLATE_BEGIN_ARGS(SAssetAuditIssueRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, FAssetAuditIssuePtr InIssue, TWeakPtr<SAssetAuditorPanel> InPanel)
		{
			Issue = InIssue;
			Panel = InPanel;
			FSuperRowType::Construct(FSuperRowType::FArguments().Padding(FMargin(0.f, 2.f)), OwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
		{
			const FMargin CellPadding(6.f, 0.f);

			if (ColumnName == SAssetAuditorPanel::ColumnSeverity)
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(CellPadding)
					[
						SNew(SImage).Image(GetSeverityBrush(Issue->Severity))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LexToText(Issue->Severity))
					];
			}
			if (ColumnName == SAssetAuditorPanel::ColumnRule)
			{
				return SNew(SBox).Padding(CellPadding).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Issue->RuleName)
					];
			}
			if (ColumnName == SAssetAuditorPanel::ColumnAsset)
			{
				// A link rather than plain text: the most common thing you want after reading
				// an issue is to see the asset.
				const FAssetData Asset = Issue->Asset;
				return SNew(SBox).Padding(CellPadding).VAlign(VAlign_Center)
					[
						SNew(SHyperlink)
						.Text(FText::FromName(Asset.AssetName))
						.ToolTipText(FText::FromString(Asset.GetObjectPathString()))
						.OnNavigate_Lambda([Asset] { BrowseTo({ Asset }); })
					];
			}
			if (ColumnName == SAssetAuditorPanel::ColumnMessage)
			{
				return SNew(SBox).Padding(CellPadding).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Issue->Message).ToolTipText(Issue->Message)
					];
			}
			if (ColumnName == SAssetAuditorPanel::ColumnFix && Issue->CanFix())
			{
				const FText Tooltip = Issue->bFixIsUndoable
					? Issue->FixDescription
					: FText::Format(LOCTEXT("FixNoUndo", "{0} (can't be undone with Ctrl+Z)"), Issue->FixDescription);
				return SNew(SBox).Padding(FMargin(4.f, 1.f)).HAlign(HAlign_Center)
					[
						SNew(SButton)
						.Text(LOCTEXT("FixButton", "Fix"))
						.ToolTipText(Tooltip)
						.OnClicked_Lambda([this]
						{
							// Weak: the row must not keep the panel alive (the tab can close while rows exist).
							if (TSharedPtr<SAssetAuditorPanel> PinnedPanel = Panel.Pin())
							{
								PinnedPanel->FixIssues({ Issue });
							}
							return FReply::Handled();
						})
					];
			}
			return SNullWidget::NullWidget;
		}

	private:
		FAssetAuditIssuePtr Issue;
		TWeakPtr<SAssetAuditorPanel> Panel;
	};
}

void SAssetAuditorPanel::Construct(const FArguments& InArgs)
{
	const FString& DefaultPath = GetDefault<UAssetAuditorSettings>()->DefaultScanPath.Path;
	if (!DefaultPath.IsEmpty())
	{
		ScanPath = DefaultPath;
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildToolbar()
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(0.f)
			[
				SAssignNew(ListView, SListView<FAssetAuditIssuePtr>)
				.ListItemsSource(&VisibleIssues)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SAssetAuditorPanel::OnGenerateRow)
				.OnMouseButtonDoubleClick(this, &SAssetAuditorPanel::OnRowDoubleClicked)
				.OnContextMenuOpening(this, &SAssetAuditorPanel::OnContextMenuOpening)
				.HeaderRow(BuildHeaderRow())
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 4.f)
		[
			SNew(STextBlock).Text(this, &SAssetAuditorPanel::GetSummaryText)
		]
	];
}

TSharedRef<SWidget> SAssetAuditorPanel::BuildToolbar()
{
	auto MakeSeverityToggle = [this](EAssetAuditSeverity Severity)
	{
		const int32 Index = static_cast<int32>(Severity);
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, Index] { return SeverityVisible[Index] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
			{
				SeverityVisible[Index] = State == ECheckBoxState::Checked;
				RefreshVisibleIssues();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f)
				[
					SNew(SImage).Image(GetSeverityBrush(Severity))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(LexToText(Severity))
				]
			];
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(8.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.Text(LOCTEXT("Scan", "Scan"))
				.ToolTipText_Lambda([this]
				{
					return CanScan()
						? LOCTEXT("ScanTooltip", "Run every enabled rule on the folder to the right.")
						: LOCTEXT("ScanWaitTooltip", "The Asset Registry is still discovering files.");
				})
				.IsEnabled(this, &SAssetAuditorPanel::CanScan)
				.OnClicked_Lambda([this] { RunScan(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
			[
				SNew(SBox).MinDesiredWidth(160.f)
				[
					SNew(SEditableTextBox)
					.Text_Lambda([this] { return FText::FromString(ScanPath); })
					.ToolTipText(LOCTEXT("PathTooltip", "Content folder to scan, recursively (e.g. /Game or /Game/Props)."))
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) { ScanPath = Text.ToString(); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				MakeSeverityToggle(EAssetAuditSeverity::Error)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				MakeSeverityToggle(EAssetAuditSeverity::Warning)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
			[
				MakeSeverityToggle(EAssetAuditSeverity::Info)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Filter by asset, rule or message"))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					SearchText = Text.ToString();
					RefreshVisibleIssues();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(12.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("FixSelected", "Fix Selected"))
				.ToolTipText(LOCTEXT("FixSelectedTooltip", "Apply the suggested fix to every selected issue that has one. Property fixes are a single undo step."))
				.IsEnabled_Lambda([this] { return GetSelectedFixableIssues().Num() > 0; })
				.OnClicked_Lambda([this] { FixIssues(GetSelectedFixableIssues()); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ExportCsv", "Export CSV"))
				.ToolTipText(LOCTEXT("ExportCsvTooltip", "Save the issues currently shown (after filters) to a CSV file."))
				.IsEnabled_Lambda([this] { return VisibleIssues.Num() > 0; })
				.OnClicked_Lambda([this] { ExportCsv(); return FReply::Handled(); })
			]
		];
}

TSharedRef<SHeaderRow> SAssetAuditorPanel::BuildHeaderRow()
{
	auto Column = [this](FName Id, const FText& Label)
	{
		return SHeaderRow::Column(Id)
			.DefaultLabel(Label)
			.SortMode_Lambda([this, Id] { return GetSortMode(Id); })
			.OnSort(this, &SAssetAuditorPanel::OnSortModeChanged);
	};

	return SNew(SHeaderRow)
		+ Column(ColumnSeverity, LOCTEXT("ColSeverity", "Severity")).FixedWidth(100.f)
		+ Column(ColumnRule, LOCTEXT("ColRule", "Rule")).FillWidth(0.18f)
		+ Column(ColumnAsset, LOCTEXT("ColAsset", "Asset")).FillWidth(0.25f)
		+ Column(ColumnMessage, LOCTEXT("ColMessage", "Message")).FillWidth(0.57f)
		+ SHeaderRow::Column(ColumnFix).DefaultLabel(LOCTEXT("ColFix", "Fix")).FixedWidth(60.f).HAlignHeader(HAlign_Center);
}

TSharedRef<ITableRow> SAssetAuditorPanel::OnGenerateRow(FAssetAuditIssuePtr Issue, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAssetAuditIssueRow, OwnerTable, Issue, SharedThis(this));
}

void SAssetAuditorPanel::OnRowDoubleClicked(FAssetAuditIssuePtr Issue)
{
	OpenSelected();
}

TSharedPtr<SWidget> SAssetAuditorPanel::OnContextMenuOpening()
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/ true, nullptr);
	const TArray<FAssetAuditIssuePtr> Fixable = GetSelectedFixableIssues();
	Menu.AddMenuEntry(
		FText::Format(LOCTEXT("FixMenu", "Fix {0} Issue(s)"), Fixable.Num()),
		LOCTEXT("FixMenuTooltip", "Apply the suggested fix to the selected issues."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Check"),
		FUIAction(
			FExecuteAction::CreateSPLambda(this, [this, Fixable] { FixIssues(Fixable); }),
			FCanExecuteAction::CreateLambda([Num = Fixable.Num()] { return Num > 0; })));
	Menu.AddSeparator();
	Menu.AddMenuEntry(
		LOCTEXT("Browse", "Show in Content Browser"),
		LOCTEXT("BrowseTooltip", "Select the asset(s) in the Content Browser."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "SystemWideCommands.FindInContentBrowser"),
		FUIAction(FExecuteAction::CreateSP(this, &SAssetAuditorPanel::BrowseToSelected)));
	Menu.AddMenuEntry(
		LOCTEXT("Open", "Open Asset"),
		LOCTEXT("OpenTooltip", "Open the asset editor for the selected asset(s)."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
		FUIAction(FExecuteAction::CreateSP(this, &SAssetAuditorPanel::OpenSelected)));
	return Menu.MakeWidget();
}

bool SAssetAuditorPanel::CanScan() const
{
	// Right after the editor starts, the registry is still crawling the disk in the background.
	// Scanning then would silently miss assets, so the button waits instead.
	return !GetAssetRegistry().IsLoadingAssets();
}

void SAssetAuditorPanel::RunScan()
{
	AllIssues.Reset();
	for (FAssetAuditIssue& Issue : FAssetAuditScanner::Scan({ FName(*ScanPath) }))
	{
		AllIssues.Add(MakeShared<FAssetAuditIssue>(MoveTemp(Issue)));
	}
	bHasScanned = true;
	RefreshVisibleIssues();
}

void SAssetAuditorPanel::FixIssues(const TArray<FAssetAuditIssuePtr>& Issues)
{
	TArray<const FAssetAuditIssue*> ToFix;
	int32 NumRenames = 0;
	for (const FAssetAuditIssuePtr& Issue : Issues)
	{
		if (Issue->CanFix())
		{
			ToFix.Add(Issue.Get());
			NumRenames += Issue->bFixIsUndoable ? 0 : 1;
		}
	}
	if (ToFix.IsEmpty())
	{
		return;
	}

	// Be explicit before doing something Ctrl+Z can't take back.
	if (NumRenames > 0)
	{
		const FText Prompt = FText::Format(LOCTEXT("RenamePrompt",
			"{0} of the selected fixes rename assets.\n\nReferences are updated and redirectors are left behind, "
			"but renames can't be undone with Ctrl+Z.\n\nContinue?"), NumRenames);
		if (FMessageDialog::Open(EAppMsgType::OkCancel, Prompt) != EAppReturnType::Ok)
		{
			return;
		}
	}

	const FAssetAuditScanner::FFixResult Result = FAssetAuditScanner::ApplyFixes(ToFix);

	FNotificationInfo Info(FText::Format(LOCTEXT("FixResult", "Asset Auditor: fixed {0}, failed {1}. Modified assets still need saving."),
		Result.Fixed, Result.Failed));
	Info.ExpireDuration = 5.f;
	if (TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(Result.Failed > 0 ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
	}

	// Paths, names and tags changed: rescan instead of patching results by hand. The scan is tag-driven
	// and the registry already reflects the in-memory changes, so this is fast and always correct.
	RunScan();
}

bool SAssetAuditorPanel::MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const
{
	// Only react to our own transactions; rescanning after every actor move would be wasteful.
	return InContext.Context == FAssetAuditScanner::TransactionContext;
}

void SAssetAuditorPanel::PostUndo(bool bSuccess)
{
	if (bSuccess && bHasScanned)
	{
		RunScan();
	}
}

void SAssetAuditorPanel::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

void SAssetAuditorPanel::ExportCsv() const
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return;
	}

	const FString DefaultDir = FPaths::ProjectSavedDir() / TEXT("AssetAudit");
	IFileManager::Get().MakeDirectory(*DefaultDir, /*Tree*/ true);

	TArray<FString> Files;
	const bool bPicked = DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		LOCTEXT("ExportTitle", "Export Asset Audit").ToString(),
		DefaultDir, TEXT("AssetAudit.csv"), TEXT("CSV file (*.csv)|*.csv"), EFileDialogFlags::None, Files);
	if (!bPicked || Files.IsEmpty())
	{
		return;
	}

	TArray<const FAssetAuditIssue*> Rows;
	for (const FAssetAuditIssuePtr& Issue : VisibleIssues)
	{
		Rows.Add(Issue.Get());
	}

	const bool bSaved = FAssetAuditReport::SaveCsv(Rows, Files[0]);
	FNotificationInfo Info(bSaved
		? FText::Format(LOCTEXT("Exported", "Exported {0} issues to {1}"), Rows.Num(), FText::FromString(Files[0]))
		: LOCTEXT("ExportFailed", "Could not write the CSV file (is it open in another program?)"));
	Info.ExpireDuration = 5.f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

TArray<FAssetAuditIssuePtr> SAssetAuditorPanel::GetSelectedFixableIssues() const
{
	TArray<FAssetAuditIssuePtr> Fixable = ListView->GetSelectedItems();
	Fixable.RemoveAll([](const FAssetAuditIssuePtr& Issue) { return !Issue->CanFix(); });
	return Fixable;
}

bool SAssetAuditorPanel::PassesFilter(const FAssetAuditIssue& Issue) const
{
	if (!SeverityVisible[static_cast<int32>(Issue.Severity)])
	{
		return false;
	}
	if (SearchText.IsEmpty())
	{
		return true;
	}
	return Issue.Asset.GetObjectPathString().Contains(SearchText)
		|| Issue.RuleName.ToString().Contains(SearchText)
		|| Issue.Message.ToString().Contains(SearchText);
}

void SAssetAuditorPanel::RefreshVisibleIssues()
{
	VisibleIssues.Reset();
	for (const FAssetAuditIssuePtr& Issue : AllIssues)
	{
		if (PassesFilter(*Issue))
		{
			VisibleIssues.Add(Issue);
		}
	}

	auto Compare = [this](const FAssetAuditIssue& A, const FAssetAuditIssue& B) -> int32
	{
		if (SortColumn == ColumnSeverity) return static_cast<int32>(A.Severity) - static_cast<int32>(B.Severity);
		if (SortColumn == ColumnRule)     return A.RuleName.CompareTo(B.RuleName);
		if (SortColumn == ColumnAsset)    return A.Asset.AssetName.Compare(B.Asset.AssetName);
		if (SortColumn == ColumnMessage)  return A.Message.CompareTo(B.Message);
		return 0;
	};

	// Stable sort: ties keep the scanner's order (severity, then path), so e.g. sorting by
	// Rule still shows errors before warnings inside each rule.
	const bool bAscending = SortMode == EColumnSortMode::Ascending;
	VisibleIssues.StableSort([&](const FAssetAuditIssuePtr& A, const FAssetAuditIssuePtr& B)
	{
		const int32 Result = Compare(*A, *B);
		return bAscending ? Result < 0 : Result > 0;
	});

	// The list only holds a pointer to VisibleIssues; it has to be told the contents changed.
	ListView->RequestListRefresh();
}

void SAssetAuditorPanel::OnSortModeChanged(EColumnSortPriority::Type Priority, const FName& Column, EColumnSortMode::Type NewMode)
{
	SortColumn = Column;
	SortMode = NewMode;
	RefreshVisibleIssues();
}

EColumnSortMode::Type SAssetAuditorPanel::GetSortMode(FName Column) const
{
	return Column == SortColumn ? SortMode : EColumnSortMode::None;
}

TArray<FAssetData> SAssetAuditorPanel::GetSelectedAssets() const
{
	TArray<FAssetData> Assets;
	for (const FAssetAuditIssuePtr& Issue : ListView->GetSelectedItems())
	{
		Assets.AddUnique(Issue->Asset);
	}
	return Assets;
}

void SAssetAuditorPanel::BrowseToSelected() const
{
	BrowseTo(GetSelectedAssets());
}

void SAssetAuditorPanel::OpenSelected() const
{
	UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	for (const FAssetData& Asset : GetSelectedAssets())
	{
		AssetEditors->OpenEditorForAsset(Asset.GetSoftObjectPath());
	}
}

FText SAssetAuditorPanel::GetSummaryText() const
{
	if (!CanScan())
	{
		return LOCTEXT("RegistryLoading", "Waiting for the Asset Registry to finish discovering assets...");
	}
	if (!bHasScanned)
	{
		return LOCTEXT("NotScanned", "Press Scan to audit the project.");
	}

	int32 Counts[3] = { 0, 0, 0 };
	for (const FAssetAuditIssuePtr& Issue : AllIssues)
	{
		++Counts[static_cast<int32>(Issue->Severity)];
	}
	return FText::Format(LOCTEXT("Summary", "{0} issues ({1} errors, {2} warnings, {3} info). Showing {4}."),
		AllIssues.Num(), Counts[2], Counts[1], Counts[0], VisibleIssues.Num());
}

#undef LOCTEXT_NAMESPACE
