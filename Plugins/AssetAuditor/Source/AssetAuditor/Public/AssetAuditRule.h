#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AssetAuditTypes.h"
#include "AssetAuditRule.generated.h"

/**
 * Base class for every audit rule. To add a rule: subclass this (in this or any other editor module),
 * override AppliesTo/Check (and optionally ApplyFix). The scanner finds it automatically via reflection.
 *
 * Why an abstract UCLASS instead of a pure C++ interface or a UINTERFACE:
 *  - Reflection gives free discovery (GetDerivedClasses), so there is no registry to keep in sync.
 *  - Config=Editor lets each rule own its thresholds as UPROPERTY(Config), saved to DefaultEditor.ini
 *    and editable in Project Settings, without a central settings class that must know about every rule.
 *  - UINTERFACE is for adding behaviour to unrelated class hierarchies; rules don't need that.
 *
 * Rules are used through their CDO and must be stateless: all inputs come from the asset and from config.
 */
UCLASS(Abstract, Config = Editor, DefaultConfig)
class ASSETAUDITOR_API UAssetAuditRule : public UObject
{
	GENERATED_BODY()

public:
	/** Stable id for CSV/CI output: the class name minus the "AssetAuditRule_" prefix. */
	FName GetRuleId() const;

	/** Taken from UCLASS(meta=(DisplayName=...)) so subclasses don't repeat themselves. */
	FText GetDisplayName() const;

	bool IsEnabled() const { return bEnabled; }
	bool ShouldRunDuringDataValidation() const { return bEnabled && bRunDuringDataValidation; }

	/** Cheap filter using only registry data (class, path). Must never load the asset. */
	virtual bool AppliesTo(const FAssetData& Asset) const { return true; }

	/**
	 * Inspect one asset and append any problems.
	 * Prefer asset registry tags; only call Asset.GetAsset() when tags can't answer the question,
	 * because loading thousands of assets is what makes naive audit tools slow and memory-hungry.
	 */
	virtual void Check(const FAssetData& Asset, TArray<FAssetAuditIssue>& OutIssues) const PURE_VIRTUAL(UAssetAuditRule::Check, );

	/**
	 * Apply Issue.FixId. The caller owns the FScopedTransaction (so "Fix selected" is one undo step);
	 * implementations must call Modify() on every object before changing it.
	 */
	virtual bool ApplyFix(const FAssetAuditIssue& Issue) const { return false; }

protected:
	/** Fills in the rule-related fields so subclasses only provide what is specific to the problem. */
	FAssetAuditIssue& AddIssue(TArray<FAssetAuditIssue>& OutIssues, const FAssetData& Asset, EAssetAuditSeverity Severity, const FText& Message) const;

	UPROPERTY(Config, EditAnywhere, Category = "Rule")
	bool bEnabled = true;

	/** Also report this rule's issues through Data Validation (on save / "Validate Assets"). */
	UPROPERTY(Config, EditAnywhere, Category = "Rule")
	bool bRunDuringDataValidation = true;
};
