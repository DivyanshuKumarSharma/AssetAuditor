#include "AssetAuditRule.h"

#define LOCTEXT_NAMESPACE "AssetAuditor"

FText LexToText(EAssetAuditSeverity Severity)
{
	switch (Severity)
	{
	case EAssetAuditSeverity::Info:    return LOCTEXT("SeverityInfo", "Info");
	case EAssetAuditSeverity::Warning: return LOCTEXT("SeverityWarning", "Warning");
	case EAssetAuditSeverity::Error:   return LOCTEXT("SeverityError", "Error");
	}
	return FText::GetEmpty();
}

const TCHAR* LexToString(EAssetAuditSeverity Severity)
{
	switch (Severity)
	{
	case EAssetAuditSeverity::Info:    return TEXT("Info");
	case EAssetAuditSeverity::Warning: return TEXT("Warning");
	case EAssetAuditSeverity::Error:   return TEXT("Error");
	}
	return TEXT("");
}

FName UAssetAuditRule::GetRuleId() const
{
	FString Name = GetClass()->GetName();
	Name.RemoveFromStart(TEXT("AssetAuditRule_"));
	return FName(*Name);
}

FText UAssetAuditRule::GetDisplayName() const
{
	return GetClass()->GetDisplayNameText();
}

FAssetAuditIssue& UAssetAuditRule::AddIssue(TArray<FAssetAuditIssue>& OutIssues, const FAssetData& Asset, EAssetAuditSeverity Severity, const FText& Message) const
{
	FAssetAuditIssue& Issue = OutIssues.AddDefaulted_GetRef();
	Issue.Asset = Asset;
	Issue.Rule = this;
	Issue.RuleId = GetRuleId();
	Issue.RuleName = GetDisplayName();
	Issue.Severity = Severity;
	Issue.Message = Message;
	return Issue;
}

#undef LOCTEXT_NAMESPACE
