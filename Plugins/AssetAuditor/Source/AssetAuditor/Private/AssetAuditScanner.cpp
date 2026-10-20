#include "AssetAuditScanner.h"

#include "AssetAuditRule.h"
#include "AssetAuditorLog.h"
#include "AssetAuditorSettings.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectHash.h"

#define LOCTEXT_NAMESPACE "AssetAuditor"

TArray<const UAssetAuditRule*> FAssetAuditScanner::GetAllRules()
{
	TArray<UClass*> RuleClasses;
	GetDerivedClasses(UAssetAuditRule::StaticClass(), RuleClasses, /*bRecursive*/ true);

	TArray<const UAssetAuditRule*> Rules;
	for (UClass* RuleClass : RuleClasses)
	{
		// NewerVersionExists: stale classes left behind by Live Coding / hot reload.
		if (RuleClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		Rules.Add(GetDefault<UAssetAuditRule>(RuleClass));
	}

	Rules.Sort([](const UAssetAuditRule& A, const UAssetAuditRule& B)
	{
		return A.GetRuleId().LexicalLess(B.GetRuleId());
	});
	return Rules;
}

TArray<const UAssetAuditRule*> FAssetAuditScanner::GetEnabledRules()
{
	TArray<const UAssetAuditRule*> Rules = GetAllRules();
	Rules.RemoveAll([](const UAssetAuditRule* Rule) { return !Rule->IsEnabled(); });
	return Rules;
}

TArray<FAssetData> FAssetAuditScanner::GatherAssets(const TArray<FName>& PackagePaths)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.PackagePaths = PackagePaths;
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	const UAssetAuditorSettings* Settings = GetDefault<UAssetAuditorSettings>();
	Assets.RemoveAll([Settings](const FAssetData& Asset)
	{
		// Redirectors are rename leftovers, not real content.
		// One-File-Per-Actor levels store every placed actor as its own package under __ExternalActors__;
		// those are level data, not content-browser assets, and would flood the naming rule.
		return Asset.IsRedirector()
			|| Asset.PackageName.ToString().Contains(TEXT("/__External"))
			|| Settings->IsExcluded(Asset.PackagePath);
	});
	return Assets;
}

void FAssetAuditScanner::CheckAsset(const FAssetData& Asset, TConstArrayView<const UAssetAuditRule*> Rules, TArray<FAssetAuditIssue>& OutIssues)
{
	for (const UAssetAuditRule* Rule : Rules)
	{
		if (Rule->AppliesTo(Asset))
		{
			Rule->Check(Asset, OutIssues);
		}
	}
}

TArray<FAssetAuditIssue> FAssetAuditScanner::Scan(const TArray<FName>& PackagePaths)
{
	// UE5 builds meshes and textures asynchronously. While a mesh is compiling, UStaticMesh::GetAssetRegistryTags
	// skips its LOD/collision tags and the registry fills the gap with the *on-disk* values, so a scan right
	// after "Add box collision" would still report the old problem. Finish pending builds first so tags are
	// current. (This is a no-op when nothing is compiling.)
	FAssetCompilingManager::Get().FinishAllCompilation();

	const TArray<const UAssetAuditRule*> Rules = GetEnabledRules();
	const TArray<FAssetData> Assets = GatherAssets(PackagePaths);

	TArray<FAssetAuditIssue> Issues;

	// Progress per asset; the dialog only appears if the scan takes longer than half a second.
	// In a commandlet this degrades to log output, so the same code path serves CI.
	FScopedSlowTask SlowTask(static_cast<float>(Assets.Num()), LOCTEXT("Scanning", "Auditing assets..."));
	SlowTask.MakeDialogDelayed(0.5f, /*bShowCancelButton*/ true);

	int32 NumChecked = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (SlowTask.ShouldCancel())
		{
			UE_LOG(LogAssetAuditor, Warning, TEXT("Scan cancelled; results are partial."));
			break;
		}
		SlowTask.EnterProgressFrame(1.f, FText::FromName(Asset.AssetName));
		CheckAsset(Asset, Rules, Issues);

		// Rules that had to load an asset leave it in memory. In the editor that's normal (it will be
		// collected eventually), but a CI run over a big project would keep growing, so collect periodically.
		// Safe because issues only hold FAssetData and weak pointers, never strong UObject references.
		if (IsRunningCommandlet() && ++NumChecked % 256 == 0)
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	}

	SortIssues(Issues);
	UE_LOG(LogAssetAuditor, Log, TEXT("Audited %d assets with %d rules: %d issues."), Assets.Num(), Rules.Num(), Issues.Num());
	return Issues;
}

void FAssetAuditScanner::SortIssues(TArray<FAssetAuditIssue>& Issues)
{
	Issues.StableSort([](const FAssetAuditIssue& A, const FAssetAuditIssue& B)
	{
		if (A.Severity != B.Severity)
		{
			return A.Severity > B.Severity;
		}
		return A.Asset.PackageName.LexicalLess(B.Asset.PackageName);
	});
}

FAssetAuditScanner::FFixResult FAssetAuditScanner::ApplyFixes(TConstArrayView<const FAssetAuditIssue*> Issues)
{
	TArray<const FAssetAuditIssue*> Undoable;
	TArray<const FAssetAuditIssue*> NonUndoable;
	for (const FAssetAuditIssue* Issue : Issues)
	{
		if (Issue->CanFix())
		{
			(Issue->bFixIsUndoable ? Undoable : NonUndoable).Add(Issue);
		}
	}

	FFixResult Result;
	auto Apply = [&Result](const FAssetAuditIssue& Issue)
	{
		const UAssetAuditRule* Rule = Issue.Rule.Get();
		if (Rule && Rule->ApplyFix(Issue))
		{
			++Result.Fixed;
		}
		else
		{
			++Result.Failed;
			UE_LOG(LogAssetAuditor, Warning, TEXT("Fix '%s' failed on %s"), *Issue.FixId.ToString(), *Issue.Asset.GetObjectPathString());
		}
	};

	if (Undoable.Num() > 0)
	{
		// The transaction records every object the rules Modify() while it is alive; Ctrl+Z restores them all.
		FScopedTransaction Transaction(TransactionContext,
			FText::Format(LOCTEXT("FixTransaction", "Asset Auditor: Fix {0} Issue(s)"), Undoable.Num()), nullptr);
		const int32 FixedBefore = Result.Fixed;
		for (const FAssetAuditIssue* Issue : Undoable)
		{
			Apply(*Issue);
		}
		if (Result.Fixed == FixedBefore)
		{
			Transaction.Cancel(); // don't leave an empty entry in the undo history
		}
	}

	// UObject::Rename calls Modify(), so a rename inside the transaction above would be recorded,
	// and Ctrl+Z would try to move the object back into a package that now holds a redirector,
	// while the files on disk stay renamed. Running renames outside any transaction keeps the
	// undo history honest: it contains only changes it can really revert.
	for (const FAssetAuditIssue* Issue : NonUndoable)
	{
		Apply(*Issue);
	}

	return Result;
}

namespace
{
	FString CsvField(FString Value)
	{
		if (Value.Contains(TEXT(",")) || Value.Contains(TEXT("\"")) || Value.Contains(TEXT("\n")))
		{
			Value.ReplaceInline(TEXT("\""), TEXT("\"\""));
			return TEXT("\"") + Value + TEXT("\"");
		}
		return Value;
	}
}

FString FAssetAuditReport::ToCsv(TConstArrayView<const FAssetAuditIssue*> Issues)
{
	FString Csv = TEXT("Severity,Rule,Asset,Class,Message,Fix\n");
	for (const FAssetAuditIssue* Issue : Issues)
	{
		Csv += FString::Join(TArray<FString>{
			LexToString(Issue->Severity),
			CsvField(Issue->RuleId.ToString()),
			CsvField(Issue->Asset.GetObjectPathString()),
			CsvField(Issue->Asset.AssetClassPath.GetAssetName().ToString()),
			CsvField(Issue->Message.ToString()),
			CsvField(Issue->FixDescription.ToString()),
		}, TEXT(","));
		Csv += TEXT("\n");
	}
	return Csv;
}

bool FAssetAuditReport::SaveCsv(TConstArrayView<const FAssetAuditIssue*> Issues, const FString& FilePath)
{
	return FFileHelper::SaveStringToFile(ToCsv(Issues), *FilePath, FFileHelper::EEncodingOptions::ForceUTF8);
}

#undef LOCTEXT_NAMESPACE
