#include "Misc/AutomationTest.h"

#include "Algo/Count.h"
#include "AssetAuditScanner.h"
#include "AssetAuditUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Texture2D.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * These tests run against the content built by Scripts/create_test_assets.py, so they cover the real
 * path: registry tags written by the importers, asset loads, the async texture/mesh builds, and the
 * reference graph. They expect that content as the script leaves it; re-run the script to reset it.
 */
namespace
{
	const FName TestContentPath(TEXT("/Game/AuditTests"));

	/** Automation can start before the editor's initial asset discovery has finished, and the Unused rule needs the whole reference graph. */
	bool PrepareTestContent(FAutomationTestBase& Test)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.WaitForCompletion();
		}

		if (FAssetAuditScanner::GatherAssets({ TestContentPath }).IsEmpty())
		{
			Test.AddError(TEXT("No test content in /Game/AuditTests. Run Plugins/AssetAuditor/Scripts/create_test_assets.py first."));
			return false;
		}
		return true;
	}

	/** On a failed count, the full list is what you need to see which rule changed behaviour. */
	void LogIssues(FAutomationTestBase& Test, const TArray<FAssetAuditIssue>& Issues)
	{
		for (const FAssetAuditIssue& Issue : Issues)
		{
			Test.AddInfo(FString::Printf(TEXT("%s [%s] %s: %s"), LexToString(Issue.Severity), *Issue.RuleId.ToString(),
				*Issue.Asset.AssetName.ToString(), *Issue.Message.ToString()));
		}
	}

	int32 CountIssues(const TArray<FAssetAuditIssue>& Issues, TFunctionRef<bool(const FAssetAuditIssue&)> Predicate)
	{
		return Algo::CountIf(Issues, Predicate);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetAuditScanTestContentTest, "AssetAuditor.Scanner.ScanTestContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetAuditScanTestContentTest::RunTest(const FString& Parameters)
{
	if (!PrepareTestContent(*this))
	{
		return false;
	}

	const TArray<FAssetAuditIssue> Issues = FAssetAuditScanner::Scan({ TestContentPath });

	// The numbers documented in the README: each broken asset contributes a known set of issues.
	// Bitwise & rather than && so every mismatching count is reported, not just the first.
	const bool bCountsMatch =
		TestEqual(TEXT("Total issues"), Issues.Num(), 17)
		& TestEqual(TEXT("Errors"), CountIssues(Issues, [](const FAssetAuditIssue& Issue) { return Issue.Severity == EAssetAuditSeverity::Error; }), 1)
		& TestEqual(TEXT("Warnings"), CountIssues(Issues, [](const FAssetAuditIssue& Issue) { return Issue.Severity == EAssetAuditSeverity::Warning; }), 9)
		& TestEqual(TEXT("Infos"), CountIssues(Issues, [](const FAssetAuditIssue& Issue) { return Issue.Severity == EAssetAuditSeverity::Info; }), 7);
	if (!bCountsMatch)
	{
		LogIssues(*this, Issues);
	}

	// The control asset guards against rules that fire on everything.
	TestEqual(TEXT("Issues on control texture T_Checker_1024"),
		CountIssues(Issues, [](const FAssetAuditIssue& Issue) { return Issue.Asset.AssetName == TEXT("T_Checker_1024"); }), 0);

	// Deterministic order is part of the contract (CI reports are diffed).
	for (int32 Index = 1; Index < Issues.Num(); ++Index)
	{
		if (Issues[Index - 1].Severity < Issues[Index].Severity)
		{
			AddError(TEXT("Issues are not sorted most severe first."));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetAuditTextureSizeFixTest, "AssetAuditor.Fixes.TextureSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetAuditTextureSizeFixTest::RunTest(const FString& Parameters)
{
	if (!PrepareTestContent(*this))
	{
		return false;
	}

	auto TextureSizeIssues = [](const TArray<FAssetAuditIssue>& Issues)
	{
		TArray<const FAssetAuditIssue*> Result;
		for (const FAssetAuditIssue& Issue : Issues)
		{
			if (Issue.RuleId == TEXT("TextureSize"))
			{
				Result.Add(&Issue);
			}
		}
		return Result;
	};

	const TArray<FAssetAuditIssue> Issues = FAssetAuditScanner::Scan({ TestContentPath });
	const TArray<const FAssetAuditIssue*> ToFix = TextureSizeIssues(Issues);
	if (!TestEqual(TEXT("Texture Size issues before fixing"), ToFix.Num(), 2))
	{
		LogIssues(*this, Issues);
		return false;
	}

	UTexture2D* Banner = LoadObject<UTexture2D>(nullptr, TEXT("/Game/AuditTests/T_Banner_NPOT.T_Banner_NPOT"));
	UTexture2D* Skybox = LoadObject<UTexture2D>(nullptr, TEXT("/Game/AuditTests/T_Skybox_4096.T_Skybox_4096"));
	if (!TestNotNull(TEXT("T_Banner_NPOT"), Banner) || !TestNotNull(TEXT("T_Skybox_4096"), Skybox))
	{
		return false;
	}

	// The test never saves; remember the packages' dirty state so it leaves no unsaved changes behind.
	const bool bBannerWasDirty = Banner->GetPackage()->IsDirty();
	const bool bSkyboxWasDirty = Skybox->GetPackage()->IsDirty();
	const ETexturePowerOfTwoSetting::Type OldPowerOfTwoMode = Banner->PowerOfTwoMode;
	const int32 OldMaxTextureSize = Skybox->MaxTextureSize;

	const FAssetAuditScanner::FFixResult Result = FAssetAuditScanner::ApplyFixes(ToFix);
	TestEqual(TEXT("Fixed"), Result.Fixed, 2);
	TestEqual(TEXT("Failed"), Result.Failed, 0);
	TestTrue(TEXT("T_Banner_NPOT pads to power of two"), Banner->PowerOfTwoMode == ETexturePowerOfTwoSetting::PadToPowerOfTwo);
	TestEqual(TEXT("T_Skybox_4096 Max Texture Size clamped to the default 2048 budget"), Skybox->MaxTextureSize, 2048);

	// The rule's own Check is the real judge of whether the fix worked.
	TestEqual(TEXT("Texture Size issues after fixing"), TextureSizeIssues(FAssetAuditScanner::Scan({ TestContentPath })).Num(), 0);

	// Put the textures back so tests that run later (e.g. the scan count) see the content as the script left it.
	// Restored by hand rather than with undo: the headless editor (-nullrhi -unattended) records no transactions.
	UE::AssetAuditor::EditProperty(Banner, GET_MEMBER_NAME_CHECKED(UTexture, PowerOfTwoMode),
		[Banner, OldPowerOfTwoMode] { Banner->PowerOfTwoMode = OldPowerOfTwoMode; });
	UE::AssetAuditor::EditProperty(Skybox, GET_MEMBER_NAME_CHECKED(UTexture, MaxTextureSize),
		[Skybox, OldMaxTextureSize] { Skybox->MaxTextureSize = OldMaxTextureSize; });
	TestEqual(TEXT("Texture Size issues after restoring"), TextureSizeIssues(FAssetAuditScanner::Scan({ TestContentPath })).Num(), 2);

	Banner->GetPackage()->SetDirtyFlag(bBannerWasDirty);
	Skybox->GetPackage()->SetDirtyFlag(bSkyboxWasDirty);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
