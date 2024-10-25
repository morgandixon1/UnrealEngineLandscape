#include "LandscapeProxy.h"
#include "LandscapeSettings.h"

void GenerateLandscape(const FString& CombinedAbsoluteFilePath, UWorld* World, int32 NumBlocks)
{
    if (!World)
    {
        return;
    }

#if WITH_EDITOR
    // Load the height map image
    TArray<uint8> RawFileData;
    if (!FFileHelper::LoadFileToArray(RawFileData, *CombinedAbsoluteFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load height map file: %s"), *CombinedAbsoluteFilePath);
        return;
    }

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create image wrapper for height map"));
        return;
    }

    // Get dimensions and ensure they're powers of two
    const int32 OriginalSizeX = ImageWrapper->GetWidth();
    const int32 OriginalSizeY = ImageWrapper->GetHeight();
    
    // Round up to the next power of two
    const int32 SizeX = FMath::RoundUpToPowerOfTwo(OriginalSizeX);
    const int32 SizeY = SizeX; // Keep it square

    UE_LOG(LogTemp, Log, TEXT("Original size: %dx%d, Adjusted to power of two: %dx%d"), 
        OriginalSizeX, OriginalSizeY, SizeX, SizeY);

    // Convert the image data to height data
    TArray<uint16> HeightData;
    TArray<uint8> RawImageData;
    if (ImageWrapper->GetRaw(ERGBFormat::Gray, 16, RawImageData))
    {
        // Create a properly sized height data array
        HeightData.SetNum(SizeX * SizeY);
        FMemory::Memzero(HeightData.GetData(), HeightData.Num() * sizeof(uint16));

        // Copy and pad the data if necessary
        const uint16* SrcData = reinterpret_cast<uint16*>(RawImageData.GetData());
        for (int32 Y = 0; Y < OriginalSizeY; Y++)
        {
            for (int32 X = 0; X < OriginalSizeX; X++)
            {
                HeightData[Y * SizeX + X] = SrcData[Y * OriginalSizeX + X];
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to get raw height map data"));
        return;
    }

    // Create landscape components
    const int32 ComponentQuads = FMath::Max(63, SizeX - 1); // Must be power of two minus one
    const int32 SubsectionQuads = ComponentQuads; // For simplicity, make subsection same size as component
    const int32 NumSubsections = 1;

    // Calculate real-world scales
    const float BlockSizeInUnrealUnits = 50000.0f; // 500 meters = 50000 unreal units
    const int32 GridSize = FMath::RoundToInt(FMath::Sqrt(static_cast<float>(NumBlocks)));
    const float TotalSizeInUnrealUnits = BlockSizeInUnrealUnits * GridSize;
    
    // Calculate scales with larger vertical scaling
    const float HorizontalScaleFactor = TotalSizeInUnrealUnits / static_cast<float>(SizeX);
    
    // New vertical scale calculation to achieve a scale factor between 20-35
    const float DesiredScale = 25.0f; // Middle of the 20-35 range
    const float VerticalScaleFactor = DesiredScale;

    // Log the scale factors for verification
    UE_LOG(LogTemp, Log, TEXT("Horizontal Scale: %f, Vertical Scale: %f"),
        HorizontalScaleFactor, VerticalScaleFactor);

    // Create the basic maps needed for import
    TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
    HeightDataPerLayers.Add(FGuid(), HeightData);

    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
    MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

    // Spawn landscape actor
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = nullptr;

    ALandscape* Landscape = World->SpawnActor<ALandscape>(ALandscape::StaticClass(),
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParams);

    if (!Landscape)
    {
        return;
    }

    // Configure landscape
    Landscape->ComponentSizeQuads = ComponentQuads;
    Landscape->SubsectionSizeQuads = SubsectionQuads;
    Landscape->NumSubsections = NumSubsections;

    // Set scale
    Landscape->SetActorScale3D(FVector(HorizontalScaleFactor, HorizontalScaleFactor, VerticalScaleFactor));

    // Import the landscape data
    Landscape->Import(
        FGuid::NewGuid(),
        0, 0, // MinX, MinY
        SizeX - 1, SizeY - 1, // MaxX, MaxY
        NumSubsections,
        SubsectionQuads,
        HeightDataPerLayers,
        nullptr,
        MaterialLayerDataPerLayers,
        ELandscapeImportAlphamapType::Additive
    );

    Landscape->CreateLandscapeInfo();

    UE_LOG(LogTemp, Log, TEXT("Created landscape with dimensions %dx%d, component size: %d, subsection size: %d"),
        SizeX, SizeY, ComponentQuads, SubsectionQuads);

#else
    UE_LOG(LogTemp, Warning, TEXT("Landscape creation is only supported in editor builds"));
#endif
}
