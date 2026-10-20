#include "Commandlets/AssetAuditCommandlet.h"

#include "AssetAuditScanner.h"
#include "AssetAuditorLog.h"
#include "AssetAuditorSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/Paths.h"

namespace
{
	enum EExitCode : int32
	{
		Pass = 0,
		IssuesFound = 1,
		BadArguments = 2,
		ReportFailed = 3,
	};
}

UAssetAuditCommandlet::UAssetAuditCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	HelpDescription = TEXT("Audits project content and writes a CSV report. Fails when issues reach the -FailOn severity.");
	HelpUsage = TEXT("-run=AssetAudit [-Path=/Game] [-Report=<file.csv>] [-FailOn=Error|Warning|Info|None]");
}

int32 UAssetAuditCommandlet::Main(const FString& Params)
{
	const UAssetAuditorSettings* Settings = GetDefault<UAssetAuditorSettings>();

	FString Path = Settings->DefaultScanPath.Path.IsEmpty() ? TEXT("/Game") : Settings->DefaultScanPath.Path;
	FParse::Value(*Params, TEXT("Path="), Path);

	FString ReportPath = FPaths::ProjectSavedDir() / TEXT("AssetAudit/AssetAudit.csv");
	FParse::Value(*Params, TEXT("Report="), ReportPath);
	ReportPath = FPaths::ConvertRelativePathToFull(ReportPath);

	// Severity threshold: project setting by default, overridable per CI job.
	TOptional<EAssetAuditSeverity> FailOn = Settings->FailOnSeverity;
	FString FailOnArg;
	if (FParse::Value(*Params, TEXT("FailOn="), FailOnArg))
	{
		if (FailOnArg.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			FailOn.Reset();
		}
		else
		{
			// Reflection turns "Warning" into the enum value, so new severities need no parsing code.
			const int64 Value = StaticEnum<EAssetAuditSeverity>()->GetValueByNameString(FailOnArg);
			if (Value == INDEX_NONE)
			{
				UE_LOG(LogAssetAuditor, Error, TEXT("Unknown -FailOn value '%s'. Usage: %s"), *FailOnArg, *HelpUsage);
				return BadArguments;
			}
			FailOn = static_cast<EAssetAuditSeverity>(Value);
		}
	}

	// The editor discovers assets asynchronously after startup; a commandlet would start scanning
	// before that finished and quietly miss assets. Block until the registry is complete.
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.SearchAllAssets(/*bSynchronousSearch*/ true);

	const TArray<FAssetAuditIssue> Issues = FAssetAuditScanner::Scan({ FName(*Path) });

	TArray<const FAssetAuditIssue*> Rows;
	int32 NumFailing = 0;
	for (const FAssetAuditIssue& Issue : Issues)
	{
		Rows.Add(&Issue);
		const bool bFails = FailOn.IsSet() && Issue.Severity >= FailOn.GetValue();
		NumFailing += bFails ? 1 : 0;

		// Display rather than Warning/Error verbosity: many CI wrappers fail a job on any logged
		// error, and the pass/fail decision belongs to -FailOn and the exit code, not to log parsing.
		UE_LOG(LogAssetAuditor, Display, TEXT("%s[%s] %s  %s: %s"), bFails ? TEXT("FAIL ") : TEXT(""),
			LexToString(Issue.Severity), *Issue.RuleId.ToString(), *Issue.Asset.GetObjectPathString(), *Issue.Message.ToString());
	}

	if (!FAssetAuditReport::SaveCsv(Rows, ReportPath))
	{
		UE_LOG(LogAssetAuditor, Error, TEXT("Could not write report to %s"), *ReportPath);
		return ReportFailed;
	}

	UE_LOG(LogAssetAuditor, Display, TEXT("Asset audit of %s: %d issue(s), %d at or above '%s'. Report: %s"),
		*Path, Issues.Num(), NumFailing, FailOn.IsSet() ? LexToString(FailOn.GetValue()) : TEXT("None"), *ReportPath);

	return NumFailing > 0 ? IssuesFound : Pass;
}
