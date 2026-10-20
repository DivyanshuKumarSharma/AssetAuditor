#include "Rules/AssetAuditRule_TextureSize.h"

#include "AssetAuditUtils.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "AssetAuditor"

namespace
{
	const FName PadFixId(TEXT("PadToPowerOfTwo"));
	const FName ClampFixId(TEXT("ClampMaxSize"));

	/** The "Dimensions" tag is written by UTexture2D::GetAssetRegistryTags as "<W>x<H>" of the imported source. */
	bool GetDimensionsTag(const FAssetData& Asset, FIntPoint& OutSize)
	{
		FString Value, Width, Height;
		if (!Asset.GetTagValue(TEXT("Dimensions"), Value) || !Value.Split(TEXT("x"), &Width, &Height))
		{
			return false;
		}
		OutSize = FIntPoint(FCString::Atoi(*Width), FCString::Atoi(*Height));
		return OutSize.X > 0 && OutSize.Y > 0;
	}

	bool IsPowerOfTwo(const FIntPoint& Size)
	{
		return FMath::IsPowerOfTwo(Size.X) && FMath::IsPowerOfTwo(Size.Y);
	}

	/** "1000x600", not the locale-grouped "1,000x600" that FText::Format would produce for ints. */
	FText SizeText(const FIntPoint& Size)
	{
		return FText::FromString(FString::Printf(TEXT("%dx%d"), Size.X, Size.Y));
	}
}

bool UAssetAuditRule_TextureSize::AppliesTo(const FAssetData& Asset) const
{
	// Exact class match: engine-internal subclasses (lightmaps, shadowmaps) have their own sizing rules.
	return Asset.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName();
}

bool UAssetAuditRule_TextureSize::IsSuspicious(const FIntPoint& Size) const
{
	return (bRequirePowerOfTwo && !IsPowerOfTwo(Size)) || FMath::Max(Size.X, Size.Y) > MaxDimension;
}

void UAssetAuditRule_TextureSize::Check(const FAssetData& Asset, TArray<FAssetAuditIssue>& OutIssues) const
{
	// Two-phase check: the registry tag tells us the source size without loading anything.
	// Most textures pass right here; we only pay for a load when the tag says there might be a
	// problem, because the final answer depends on per-texture settings that aren't in the registry
	// (padding mode, Max Texture Size, LOD group).
	FIntPoint Size;
	const bool bHasTag = GetDimensionsTag(Asset, Size);
	if (bHasTag && !IsSuspicious(Size))
	{
		return;
	}

	const UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset());
	if (!Texture)
	{
		return;
	}
	if (!bHasTag)
	{
		Size = Texture->GetImportedSize();
	}

	const bool bIsUI = Texture->LODGroup == TEXTUREGROUP_UI;
	if (bRequirePowerOfTwo && !IsPowerOfTwo(Size)
		&& Texture->PowerOfTwoMode == ETexturePowerOfTwoSetting::None
		&& !(bAllowNonPowerOfTwoForUI && bIsUI))
	{
		FAssetAuditIssue& Issue = AddIssue(OutIssues, Asset, EAssetAuditSeverity::Warning,
			FText::Format(LOCTEXT("NotPow2", "{0} is not a power of two: no mipmaps or streaming."), SizeText(Size)));
		Issue.FixId = PadFixId;
		Issue.FixDescription = LOCTEXT("PadFix", "Set Padding to 'Pad to Power of Two'");
	}

	const int32 Largest = FMath::Max(Size.X, Size.Y);
	const int32 Effective = Texture->MaxTextureSize > 0 ? FMath::Min(Largest, Texture->MaxTextureSize) : Largest;
	if (Effective > MaxDimension)
	{
		FAssetAuditIssue& Issue = AddIssue(OutIssues, Asset, EAssetAuditSeverity::Warning,
			FText::Format(LOCTEXT("OverBudget", "{0} exceeds the {1}px budget."), SizeText(Size), FText::AsNumber(MaxDimension, &FNumberFormattingOptions::DefaultNoGrouping())));
		Issue.FixId = ClampFixId;
		Issue.FixDescription = FText::Format(LOCTEXT("ClampFix", "Set Max Texture Size to {0}"), FText::AsNumber(MaxDimension, &FNumberFormattingOptions::DefaultNoGrouping()));
	}
}

bool UAssetAuditRule_TextureSize::ApplyFix(const FAssetAuditIssue& Issue) const
{
	UTexture2D* Texture = Cast<UTexture2D>(Issue.Asset.GetAsset());
	if (!Texture)
	{
		return false;
	}

	// Both fixes are non-destructive property changes: the source art stays untouched and Ctrl+Z restores
	// the old value. Resampling the source would lose data and couldn't be undone.
	if (Issue.FixId == PadFixId)
	{
		UE::AssetAuditor::EditProperty(Texture, GET_MEMBER_NAME_CHECKED(UTexture, PowerOfTwoMode),
			[Texture] { Texture->PowerOfTwoMode = ETexturePowerOfTwoSetting::PadToPowerOfTwo; });
		return true;
	}
	if (Issue.FixId == ClampFixId)
	{
		UE::AssetAuditor::EditProperty(Texture, GET_MEMBER_NAME_CHECKED(UTexture, MaxTextureSize),
			[Texture, this] { Texture->MaxTextureSize = MaxDimension; });
		return true;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
