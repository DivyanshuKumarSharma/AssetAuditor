#include "Rules/AssetAuditRule_Naming.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"

#define LOCTEXT_NAMESPACE "AssetAuditor"

namespace
{
	const FName RenameFixId(TEXT("Rename"));
}

UAssetAuditRule_Naming::UAssetAuditRule_Naming()
{
	// Defaults based on the widely used community UE style guide. Config (DefaultEditor.ini) overrides
	// this whole list once someone edits it in Project Settings.
	auto Add = [this](const TCHAR* ClassPath, const TCHAR* Prefix)
	{
		FAssetAuditNamingConvention& Convention = Conventions.AddDefaulted_GetRef();
		Convention.AssetClass = TSoftClassPtr<UObject>(FSoftObjectPath(ClassPath));
		Convention.Prefix = Prefix;
	};
	Add(TEXT("/Script/Engine.Texture2D"),                  TEXT("T_"));
	Add(TEXT("/Script/Engine.TextureCube"),                TEXT("TC_"));
	Add(TEXT("/Script/Engine.TextureRenderTarget2D"),      TEXT("RT_"));
	Add(TEXT("/Script/Engine.StaticMesh"),                 TEXT("SM_"));
	Add(TEXT("/Script/Engine.SkeletalMesh"),               TEXT("SK_"));
	Add(TEXT("/Script/Engine.Skeleton"),                   TEXT("SKEL_"));
	Add(TEXT("/Script/Engine.PhysicsAsset"),               TEXT("PHYS_"));
	Add(TEXT("/Script/Engine.AnimSequence"),               TEXT("A_"));
	Add(TEXT("/Script/Engine.AnimMontage"),                TEXT("AM_"));
	Add(TEXT("/Script/Engine.BlendSpace"),                 TEXT("BS_"));
	Add(TEXT("/Script/Engine.Material"),                   TEXT("M_"));
	Add(TEXT("/Script/Engine.MaterialInstanceConstant"),   TEXT("MI_"));
	Add(TEXT("/Script/Engine.MaterialFunction"),           TEXT("MF_"));
	Add(TEXT("/Script/Engine.MaterialParameterCollection"),TEXT("MPC_"));
	Add(TEXT("/Script/Engine.Blueprint"),                  TEXT("BP_"));
	Add(TEXT("/Script/Engine.AnimBlueprint"),              TEXT("ABP_"));
	Add(TEXT("/Script/UMGEditor.WidgetBlueprint"),         TEXT("WBP_"));
	Add(TEXT("/Script/Engine.SoundWave"),                  TEXT("S_"));
	Add(TEXT("/Script/Engine.SoundCue"),                   TEXT("SC_"));
	Add(TEXT("/Script/Engine.DataTable"),                  TEXT("DT_"));
	Add(TEXT("/Script/Engine.DataAsset"),                  TEXT("DA_"));
	Add(TEXT("/Script/Niagara.NiagaraSystem"),             TEXT("NS_"));
	Add(TEXT("/Script/Niagara.NiagaraEmitter"),            TEXT("NE_"));
}

const FAssetAuditNamingConvention* UAssetAuditRule_Naming::FindConvention(const UClass* AssetClass) const
{
	const FAssetAuditNamingConvention* Best = nullptr;
	const UClass* BestClass = nullptr;

	for (const FAssetAuditNamingConvention& Convention : Conventions)
	{
		// Get() only resolves classes already in memory; if a class's module isn't loaded,
		// no asset of that type can be loaded either, so skipping it is correct.
		const UClass* ConventionClass = Convention.AssetClass.Get();
		if (ConventionClass && AssetClass->IsChildOf(ConventionClass)
			&& (!BestClass || ConventionClass->IsChildOf(BestClass)))
		{
			Best = &Convention;
			BestClass = ConventionClass;
		}
	}
	return Best;
}

FString UAssetAuditRule_Naming::GetExpectedName(const FAssetData& Asset) const
{
	// FAssetData::GetClass() is a lookup of the class name stored in the registry: no asset load.
	const UClass* AssetClass = Asset.GetClass();
	const FAssetAuditNamingConvention* Convention = AssetClass ? FindConvention(AssetClass) : nullptr;
	if (!Convention || Convention->Prefix.IsEmpty())
	{
		return FString();
	}

	const FString Name = Asset.AssetName.ToString();
	if (Name.StartsWith(Convention->Prefix, ESearchCase::CaseSensitive))
	{
		return FString();
	}

	// Strip any *other* known prefix first, so a texture called SM_Rock becomes T_Rock, not T_SM_Rock.
	// Longest match wins so "MI_" is preferred over "M_" when both could apply.
	FString Stem = Name;
	int32 LongestPrefix = 0;
	for (const FAssetAuditNamingConvention& Other : Conventions)
	{
		if (Other.Prefix.Len() > LongestPrefix && Name.StartsWith(Other.Prefix, ESearchCase::IgnoreCase) && Name.Len() > Other.Prefix.Len())
		{
			LongestPrefix = Other.Prefix.Len();
		}
	}
	Stem.RightChopInline(LongestPrefix);

	return Convention->Prefix + Stem;
}

void UAssetAuditRule_Naming::Check(const FAssetData& Asset, TArray<FAssetAuditIssue>& OutIssues) const
{
	const FString Expected = GetExpectedName(Asset);
	if (Expected.IsEmpty())
	{
		return;
	}

	FAssetAuditIssue& Issue = AddIssue(OutIssues, Asset, EAssetAuditSeverity::Warning,
		FText::Format(LOCTEXT("NamingMessage", "{0} name doesn't follow the convention; expected \"{1}\"."),
			Asset.GetClass()->GetDisplayNameText(), FText::FromString(Expected)));
	Issue.FixId = RenameFixId;
	Issue.FixDescription = FText::Format(LOCTEXT("NamingFix", "Rename to {0}"), FText::FromString(Expected));
	// Renames move packages on disk and leave redirectors; the transaction buffer only tracks UObject state.
	Issue.bFixIsUndoable = false;
}

bool UAssetAuditRule_Naming::ApplyFix(const FAssetAuditIssue& Issue) const
{
	if (Issue.FixId != RenameFixId)
	{
		return false;
	}

	// Recompute rather than trusting the scan: config or the asset may have changed since.
	const FString Expected = GetExpectedName(Issue.Asset);
	UObject* Asset = Issue.Asset.GetAsset();
	if (Expected.IsEmpty() || !Asset)
	{
		return false;
	}

	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	const FString PackagePath = Issue.Asset.PackagePath.ToString();

	// If T_Rock already exists we get T_Rock1 instead of failing.
	FString UniquePackageName, UniqueAssetName;
	AssetTools.CreateUniqueAssetName(PackagePath / Expected, FString(), UniquePackageName, UniqueAssetName);

	// IAssetTools (not a raw UObject::Rename) also fixes up every referencer and leaves a redirector
	// so content that isn't loaded right now still resolves.
	TArray<FAssetRenameData> Renames;
	Renames.Emplace(Asset, PackagePath, UniqueAssetName);
	return AssetTools.RenameAssets(Renames);
}

#undef LOCTEXT_NAMESPACE
