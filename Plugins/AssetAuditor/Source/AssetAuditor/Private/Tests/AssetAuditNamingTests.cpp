#include "Misc/AutomationTest.h"

#include "Animation/AnimBlueprint.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Rules/AssetAuditRule_Naming.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/**
	 * Registry-style asset data built in memory. GetExpectedName only reads the name and class,
	 * so the naming logic is tested without creating (or depending on) any packages.
	 */
	FAssetData MakeAssetData(const TCHAR* AssetName, const UClass* AssetClass)
	{
		const FString PackagePath = TEXT("/Game/AuditTests/Naming");
		return FAssetData(FName(PackagePath / AssetName), FName(PackagePath), FName(AssetName), AssetClass->GetClassPathName());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAssetAuditNamingExpectedNameTest, "AssetAuditor.Rules.Naming.ExpectedName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAssetAuditNamingExpectedNameTest::RunTest(const FString& Parameters)
{
	// The CDO is what the scanner uses, so this tests the conventions that actually ship (defaults + config).
	const UAssetAuditRule_Naming* Rule = GetDefault<UAssetAuditRule_Naming>();
	auto ExpectName = [this, Rule](const TCHAR* What, const TCHAR* AssetName, const UClass* AssetClass, const TCHAR* Expected)
	{
		TestEqual(What, Rule->GetExpectedName(MakeAssetData(AssetName, AssetClass)), FString(Expected));
	};

	ExpectName(TEXT("Missing prefix is added"), TEXT("Rock_Diffuse"), UTexture2D::StaticClass(), TEXT("T_Rock_Diffuse"));
	ExpectName(TEXT("Another type's prefix is replaced, not kept (T_WrongPrefix, not T_SM_WrongPrefix)"),
		TEXT("SM_WrongPrefix"), UTexture2D::StaticClass(), TEXT("T_WrongPrefix"));
	ExpectName(TEXT("Compliant name needs no change"), TEXT("T_Rock"), UTexture2D::StaticClass(), TEXT(""));
	ExpectName(TEXT("Class with no convention is ignored"), TEXT("SomeLevel"), UWorld::StaticClass(), TEXT(""));

	// MI_ and M_ share a first letter; the longest known prefix is the one stripped,
	// so neither direction leaves a stray "I_" or doubles up the prefix.
	ExpectName(TEXT("Material instance named like a material gets MI_"),
		TEXT("M_RockInstance"), UMaterialInstanceConstant::StaticClass(), TEXT("MI_RockInstance"));
	ExpectName(TEXT("Material named like an instance loses the whole MI_"),
		TEXT("MI_Rock"), UMaterial::StaticClass(), TEXT("M_Rock"));

	// UAnimBlueprint derives from UBlueprint, so both conventions match; the most derived class must win.
	ExpectName(TEXT("Blueprint gets BP_"), TEXT("PlayerSpawner"), UBlueprint::StaticClass(), TEXT("BP_PlayerSpawner"));
	ExpectName(TEXT("AnimBlueprint gets ABP_, not BP_"), TEXT("PlayerAnim"), UAnimBlueprint::StaticClass(), TEXT("ABP_PlayerAnim"));
	ExpectName(TEXT("AnimBlueprint named like a Blueprint gets ABP_"), TEXT("BP_PlayerAnim"), UAnimBlueprint::StaticClass(), TEXT("ABP_PlayerAnim"));
	ExpectName(TEXT("Compliant AnimBlueprint needs no change"), TEXT("ABP_PlayerAnim"), UAnimBlueprint::StaticClass(), TEXT(""));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
