#include "Misc/AutomationTest.h"

#include "AssetAuditScanner.h"
#include "Engine/Texture2D.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FAssetAuditIssue MakeIssue(const TCHAR* AssetName, EAssetAuditSeverity Severity, const TCHAR* Message, const TCHAR* Fix)
	{
		FAssetAuditIssue Issue;
		const FString PackageName = FString(TEXT("/Game/Props/")) + AssetName;
		Issue.Asset = FAssetData(FName(PackageName), FName(TEXT("/Game/Props")), FName(AssetName), UTexture2D::StaticClass()->GetClassPathName());
		Issue.RuleId = TEXT("Naming");
		Issue.Severity = Severity;
		Issue.Message = FText::FromString(Message);
		Issue.FixDescription = FText::FromString(Fix);
		return Issue;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetAuditReportCsvEscapingTest, "AssetAuditor.Report.CsvEscaping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetAuditReportCsvEscapingTest::RunTest(const FString& Parameters)
{
	// Rule messages quote asset names and contain commas; RFC 4180 quoting keeps them in one cell in Excel and CI parsers.
	const FAssetAuditIssue Plain = MakeIssue(TEXT("T_Rock"), EAssetAuditSeverity::Info, TEXT("Nothing references this asset."), TEXT(""));
	const FAssetAuditIssue QuotesAndCommas = MakeIssue(TEXT("Rock"), EAssetAuditSeverity::Warning,
		TEXT("Name doesn't follow the convention; expected \"T_Rock\", not \"Rock\"."), TEXT("Rename to T_Rock, leave a redirector"));
	const FAssetAuditIssue Newline = MakeIssue(TEXT("T_Banner"), EAssetAuditSeverity::Error, TEXT("Line one\nLine two"), TEXT("Pad"));

	const TArray<const FAssetAuditIssue*> Rows = { &Plain, &QuotesAndCommas, &Newline };
	const FString Csv = FAssetAuditReport::ToCsv(Rows);

	const FString Expected =
		TEXT("Severity,Rule,Asset,Class,Message,Fix\n")
		// No special characters: fields stay unquoted.
		TEXT("Info,Naming,/Game/Props/T_Rock.T_Rock,Texture2D,Nothing references this asset.,\n")
		// Quotes are doubled and the field is wrapped; a comma alone also forces wrapping.
		TEXT("Warning,Naming,/Game/Props/Rock.Rock,Texture2D,\"Name doesn't follow the convention; expected \"\"T_Rock\"\", not \"\"Rock\"\".\",\"Rename to T_Rock, leave a redirector\"\n")
		// An embedded newline must be quoted or it would start a new record.
		TEXT("Error,Naming,/Game/Props/T_Banner.T_Banner,Texture2D,\"Line one\nLine two\",Pad\n");

	TestEqual(TEXT("CSV output"), Csv, Expected);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
