#include "Rules/AssetAuditRule_StaticMesh.h"

#include "AssetAuditUtils.h"
#include "BodySetupEnums.h"
#include "Editor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "AssetAuditor"

namespace
{
	const FName GenerateLODsFixId(TEXT("GenerateLODs"));
	const FName AddCollisionFixId(TEXT("AddBoxCollision"));

	/** Everything the rule needs, readable from registry tags (fast) or from the loaded mesh (fallback). */
	struct FMeshStats
	{
		int32 NumLODs = 0;
		int32 NumTriangles = 0;
		int32 NumCollisionPrims = 0;
		bool bNanite = false;
		bool bComplexAsSimple = false;
		bool bNoCollisionProfile = false;

		/** Tags written by UStaticMesh::GetAssetRegistryTags. */
		bool ReadFromTags(const FAssetData& Asset)
		{
			using UE::AssetAuditor::GetIntTag;
			if (!GetIntTag(Asset, TEXT("LODs"), NumLODs)
				|| !GetIntTag(Asset, TEXT("Triangles"), NumTriangles)
				|| !GetIntTag(Asset, TEXT("CollisionPrims"), NumCollisionPrims))
			{
				return false;
			}
			FString Value;
			bNanite = Asset.GetTagValue(TEXT("NaniteEnabled"), Value) && Value == TEXT("True");
			bComplexAsSimple = Asset.GetTagValue(TEXT("CollisionComplexity"), Value) && Value == LexToString(CTF_UseComplexAsSimple);
			bNoCollisionProfile = Asset.GetTagValue(TEXT("DefaultCollision"), Value) && FName(*Value) == UCollisionProfile::NoCollision_ProfileName;
			return true;
		}

		void ReadFromMesh(const UStaticMesh& Mesh)
		{
			NumLODs = Mesh.GetNumLODs();
			NumTriangles = NumLODs > 0 ? Mesh.GetNumTriangles(0) : 0;
			bNanite = Mesh.IsNaniteEnabled();
			if (const UBodySetup* BodySetup = Mesh.GetBodySetup())
			{
				NumCollisionPrims = BodySetup->AggGeom.GetElementCount();
				bComplexAsSimple = BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple;
				bNoCollisionProfile = BodySetup->DefaultInstance.GetCollisionProfileName() == UCollisionProfile::NoCollision_ProfileName;
			}
		}
	};
}

bool UAssetAuditRule_StaticMesh::AppliesTo(const FAssetData& Asset) const
{
	return Asset.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName();
}

void UAssetAuditRule_StaticMesh::Check(const FAssetData& Asset, TArray<FAssetAuditIssue>& OutIssues) const
{
	FMeshStats Stats;
	if (!Stats.ReadFromTags(Asset))
	{
		// Only assets saved before these tags existed get here.
		const UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
		if (!Mesh)
		{
			return;
		}
		Stats.ReadFromMesh(*Mesh);
	}

	if (bRequireLODs && Stats.NumLODs < MinLODCount && Stats.NumTriangles >= MinTrianglesForLODs
		&& !(bIgnoreNaniteMeshes && Stats.bNanite))
	{
		FAssetAuditIssue& Issue = AddIssue(OutIssues, Asset, EAssetAuditSeverity::Warning,
			FText::Format(LOCTEXT("NoLODs", "{0} LOD(s) for {1} triangles; expected at least {2}."),
				Stats.NumLODs, Stats.NumTriangles, MinLODCount));
		Issue.FixId = GenerateLODsFixId;
		Issue.FixDescription = FText::Format(LOCTEXT("LODFix", "Generate {0} LODs"), GeneratedLODCount);
	}

	// No simple shapes is only a problem if the mesh is meant to collide and isn't using its render mesh
	// as collision on purpose. Otherwise player movement and physics queries go straight through it.
	if (bRequireSimpleCollision && Stats.NumCollisionPrims == 0 && !Stats.bComplexAsSimple && !Stats.bNoCollisionProfile)
	{
		FAssetAuditIssue& Issue = AddIssue(OutIssues, Asset, EAssetAuditSeverity::Error,
			LOCTEXT("NoCollision", "No simple collision: physics and character movement will pass through it."));
		Issue.FixId = AddCollisionFixId;
		Issue.FixDescription = LOCTEXT("CollisionFix", "Add box collision");
	}
}

bool UAssetAuditRule_StaticMesh::ApplyFix(const FAssetAuditIssue& Issue) const
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(Issue.Asset.GetAsset());

	// Linking against StaticMeshEditor isn't enough: an editor subsystem is only created when its module
	// is loaded through the module manager. Until someone opens a mesh editor that hasn't happened,
	// GetEditorSubsystem returns null, and the fix would fail silently in a fresh editor session.
	FModuleManager::Get().LoadModuleChecked(TEXT("StaticMeshEditor"));
	UStaticMeshEditorSubsystem* MeshSubsystem = GEditor ? GEditor->GetEditorSubsystem<UStaticMeshEditorSubsystem>() : nullptr;
	if (!Mesh || !MeshSubsystem)
	{
		return false;
	}

	// We reuse the engine's own scripting helpers instead of re-implementing mesh reduction or
	// box fitting. They call Modify() on the mesh/body setup but don't open a transaction; our
	// caller has one open, which is exactly what makes these fixes undoable.
	if (Issue.FixId == AddCollisionFixId)
	{
		return MeshSubsystem->AddSimpleCollisions(Mesh, EScriptCollisionShapeType::Box) != INDEX_NONE;
	}
	if (Issue.FixId == GenerateLODsFixId)
	{
		FStaticMeshReductionOptions Options;
		Options.bAutoComputeLODScreenSize = true;
		for (int32 LODIndex = 0; LODIndex < GeneratedLODCount; ++LODIndex)
		{
			FStaticMeshReductionSettings& Settings = Options.ReductionSettings.AddDefaulted_GetRef();
			Settings.PercentTriangles = FMath::Pow(0.5f, static_cast<float>(LODIndex));
			Settings.ScreenSize = FMath::Pow(0.5f, static_cast<float>(LODIndex)); // ignored when auto-computing
		}
		return MeshSubsystem->SetLods(Mesh, Options) > 0;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
