#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "AssetAuditTypes.generated.h"

class UAssetAuditRule;

/** UENUM (not a plain enum) so it can be a config/Project Settings property, e.g. "fail CI at Error". */
UENUM(BlueprintType)
enum class EAssetAuditSeverity : uint8
{
	Info,
	Warning,
	Error,
};

ASSETAUDITOR_API FText LexToText(EAssetAuditSeverity Severity);
ASSETAUDITOR_API const TCHAR* LexToString(EAssetAuditSeverity Severity);

/**
 * One problem on one asset.
 * A plain C++ struct rather than a USTRUCT: it is transient UI/report data that is never serialized,
 * and it holds the rule via TWeakObjectPtr, which doesn't need reflection to be GC-safe.
 */
struct ASSETAUDITOR_API FAssetAuditIssue
{
	FAssetData Asset;

	/** Rule that raised the issue; it is also the object asked to fix it. */
	TWeakObjectPtr<const UAssetAuditRule> Rule;

	/** Copied from the rule so sorting/CSV never has to touch the rule object. */
	FName RuleId;
	FText RuleName;

	EAssetAuditSeverity Severity = EAssetAuditSeverity::Warning;
	FText Message;

	/** Which fix to apply. A rule can offer different fixes for different problems; NAME_None means "no automatic fix". */
	FName FixId;
	FText FixDescription;

	/** False for operations the transaction buffer can't revert (e.g. renaming packages on disk). Shown in the UI. */
	bool bFixIsUndoable = true;

	bool CanFix() const { return !FixId.IsNone() && Rule.IsValid(); }
};
