#include "Validation/AssetAuditorValidator.h"

#include "AssetAuditRule.h"
#include "AssetAuditScanner.h"
#include "Logging/TokenizedMessage.h"

#define LOCTEXT_NAMESPACE "AssetAuditor"

namespace
{
	TArray<const UAssetAuditRule*> GetValidationRules()
	{
		TArray<const UAssetAuditRule*> Rules = FAssetAuditScanner::GetAllRules();
		Rules.RemoveAll([](const UAssetAuditRule* Rule) { return !Rule->ShouldRunDuringDataValidation(); });
		return Rules;
	}
}

bool UAssetAuditorValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const
{
	if (!InObject)
	{
		return false;
	}
	for (const UAssetAuditRule* Rule : GetValidationRules())
	{
		if (Rule->AppliesTo(InAssetData))
		{
			return true;
		}
	}
	return false;
}

EDataValidationResult UAssetAuditorValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
	// The subsystem hands us the registry's on-disk FAssetData, whose tags describe the asset as it was
	// last saved. When validating on save (after you just added collision, say), those tags are stale.
	// Building FAssetData from the loaded object re-gathers the tags from its current state.
	const FAssetData CurrentData(InAsset);

	TArray<FAssetAuditIssue> Issues;
	FAssetAuditScanner::CheckAsset(CurrentData, GetValidationRules(), Issues);

	for (const FAssetAuditIssue& Issue : Issues)
	{
		// Data Validation appends its own "." and validator name, so drop ours to avoid "it.. (Validator)".
		FString Body = Issue.Message.ToString();
		Body.RemoveFromEnd(TEXT("."));
		const FText Message = FText::Format(LOCTEXT("ValidationMessage", "[Asset Auditor] {0}: {1}"), Issue.RuleName, FText::FromString(Body));
		switch (Issue.Severity)
		{
		case EAssetAuditSeverity::Error:   AssetFails(InAsset, Message); break;   // marks the asset invalid (blocks submit)
		case EAssetAuditSeverity::Warning: AssetWarning(InAsset, Message); break;
		case EAssetAuditSeverity::Info:    AssetMessage(InAssetData, EMessageSeverity::Info, Message); break;
		}
	}

	if (GetValidationResult() != EDataValidationResult::Invalid)
	{
		AssetPasses(InAsset);
	}
	return GetValidationResult();
}

#undef LOCTEXT_NAMESPACE
