#pragma once

#include "CoreMinimal.h"
#include "AssetAuditTypes.h"

class UAssetAuditRule;

/**
 * The audit engine, with no UI. The Slate panel, the Data Validation hook and the CI commandlet
 * all call into this, so every entry point reports exactly the same issues.
 */
class ASSETAUDITOR_API FAssetAuditScanner
{
public:
	/** CDOs of every concrete UAssetAuditRule subclass, enabled or not, sorted by id for stable output. */
	static TArray<const UAssetAuditRule*> GetAllRules();

	/** Enabled rules only. */
	static TArray<const UAssetAuditRule*> GetEnabledRules();

	/** Registry query: every auditable asset under the given paths (recursive). Loads nothing. */
	static TArray<FAssetData> GatherAssets(const TArray<FName>& PackagePaths);

	/** Run the given rules against one asset. */
	static void CheckAsset(const FAssetData& Asset, TConstArrayView<const UAssetAuditRule*> Rules, TArray<FAssetAuditIssue>& OutIssues);

	/** Gather + check, with a cancellable progress dialog in the editor. Results sorted most severe first. */
	static TArray<FAssetAuditIssue> Scan(const TArray<FName>& PackagePaths);

	/** Most severe first, then by asset path, so output is deterministic (important for diffing CI reports). */
	static void SortIssues(TArray<FAssetAuditIssue>& Issues);

	/** Context string on every transaction we open, so undo listeners can recognise our transactions. */
	static constexpr const TCHAR* TransactionContext = TEXT("AssetAuditor");

	struct FFixResult
	{
		int32 Fixed = 0;
		int32 Failed = 0;
	};

	/**
	 * Apply each issue's fix. Undoable fixes share one FScopedTransaction, so a batch is a single Ctrl+Z.
	 * Non-undoable fixes (renames) deliberately run outside it; see the .cpp for why.
	 */
	static FFixResult ApplyFixes(TConstArrayView<const FAssetAuditIssue*> Issues);
};

/** CSV output shared by the panel's Export button and the CI commandlet. */
class ASSETAUDITOR_API FAssetAuditReport
{
public:
	static FString ToCsv(TConstArrayView<const FAssetAuditIssue*> Issues);

	/** Writes UTF-8 with a BOM so Excel detects the encoding. */
	static bool SaveCsv(TConstArrayView<const FAssetAuditIssue*> Issues, const FString& FilePath);
};
