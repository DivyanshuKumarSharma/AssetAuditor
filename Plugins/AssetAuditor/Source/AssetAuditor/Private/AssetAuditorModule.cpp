#include "AssetAuditorModule.h"

#include "AssetAuditorLog.h"
#include "AssetAuditRule.h"
#include "AssetAuditScanner.h"
#include "ISettingsModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "UI/SAssetAuditorPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "AssetAuditor"

DEFINE_LOG_CATEGORY(LogAssetAuditor);

const FName FAssetAuditorModule::AuditorTabName(TEXT("AssetAuditor"));

// Headless entry point for quick testing from the editor's console: "AssetAuditor.Scan [/Game/Path]".
static FAutoConsoleCommand GScanCommand(
	TEXT("AssetAuditor.Scan"),
	TEXT("Run every enabled audit rule and log the results. Optional argument: package path (default /Game)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FName Path = Args.Num() > 0 ? FName(*Args[0]) : FName(TEXT("/Game"));
		for (const FAssetAuditIssue& Issue : FAssetAuditScanner::Scan({ Path }))
		{
			UE_LOG(LogAssetAuditor, Display, TEXT("[%s] %s  %s: %s"), LexToString(Issue.Severity),
				*Issue.RuleId.ToString(), *Issue.Asset.GetObjectPathString(), *Issue.Message.ToString());
		}
	}));

// Bulk fix without the UI, e.g. "AssetAuditor.Fix TextureSize /Game/Props". Typing a console command is
// already explicit consent, so unlike the panel there's no rename confirmation dialog.
static FAutoConsoleCommand GFixCommand(
	TEXT("AssetAuditor.Fix"),
	TEXT("Apply every available fix for one rule (or All). Usage: AssetAuditor.Fix <RuleId|All> [/Game/Path]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogAssetAuditor, Warning, TEXT("Usage: AssetAuditor.Fix <RuleId|All> [/Game/Path]"));
			return;
		}
		const FName RuleId(*Args[0]);
		const bool bAll = Args[0].Equals(TEXT("All"), ESearchCase::IgnoreCase);
		const FName Path = Args.Num() > 1 ? FName(*Args[1]) : FName(TEXT("/Game"));

		const TArray<FAssetAuditIssue> Issues = FAssetAuditScanner::Scan({ Path });
		TArray<const FAssetAuditIssue*> ToFix;
		for (const FAssetAuditIssue& Issue : Issues)
		{
			if (bAll || Issue.RuleId == RuleId)
			{
				ToFix.Add(&Issue);
			}
		}
		const FAssetAuditScanner::FFixResult Result = FAssetAuditScanner::ApplyFixes(ToFix);
		UE_LOG(LogAssetAuditor, Display, TEXT("AssetAuditor.Fix %s: fixed %d, failed %d."), *Args[0], Result.Fixed, Result.Failed);
	}));

void FAssetAuditorModule::StartupModule()
{
	// Commandlets (CI) load editor modules too, but there is no UI to attach to.
	if (IsRunningCommandlet())
	{
		return;
	}

	// A nomad tab can dock anywhere and is not tied to a specific asset editor.
	// Putting it in the Tools category is what makes it appear under the editor's Tools menu,
	// and the tab manager also remembers its layout between sessions for free.
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AuditorTabName,
			FOnSpawnTab::CreateRaw(this, &FAssetAuditorModule::SpawnAuditorTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Asset Auditor"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Scan the project for naming, texture, mesh and unused-asset problems."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"));

	RegisterRuleSettings();
}

void FAssetAuditorModule::ShutdownModule()
{
	// CreateRaw binds to 'this', so the spawner must be gone before the module is destroyed.
	if (!IsRunningCommandlet() && FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AuditorTabName);
	}

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings")))
	{
		for (const FName& Section : RegisteredRuleSections)
		{
			SettingsModule->UnregisterSettings(TEXT("Project"), TEXT("Plugins"), Section);
		}
	}
	RegisteredRuleSections.Reset();
}

void FAssetAuditorModule::RegisterRuleSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings"));
	if (!SettingsModule)
	{
		return;
	}

	// UDeveloperSettings is just a convenience that does this registration automatically. Doing it by
	// hand for each rule CDO gives every rule its own Project Settings page without making rules inherit
	// from a settings class. Edits land in the rule's [/Script/AssetAuditor.<RuleClass>] section of
	// DefaultEditor.ini because the rule classes are Config=Editor, DefaultConfig.
	for (const UAssetAuditRule* Rule : FAssetAuditScanner::GetAllRules())
	{
		const FName Section(*FString::Printf(TEXT("AssetAuditor_%s"), *Rule->GetRuleId().ToString()));
		SettingsModule->RegisterSettings(TEXT("Project"), TEXT("Plugins"), Section,
			FText::Format(LOCTEXT("RuleSettingsName", "Asset Auditor - {0}"), Rule->GetDisplayName()),
			Rule->GetClass()->GetToolTipText(),
			GetMutableDefault<UAssetAuditRule>(Rule->GetClass()));
		RegisteredRuleSections.Add(Section);
	}
}

TSharedRef<SDockTab> FAssetAuditorModule::SpawnAuditorTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SAssetAuditorPanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAssetAuditorModule, AssetAuditor)
