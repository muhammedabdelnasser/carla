// Copyright (c) 2019 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla.h"
#include "Carla/Actor/CarlaBlueprintRegistry.h"
#include "Carla/Game/CarlaStatics.h"

namespace CommonAttributes {
  static const FString PATH = FPaths::ProjectContentDir() + FString("/Carla/Config/");
  static const FString DEFAULT = TEXT("Default");
  static const FString DEFINITIONS = TEXT("definitions");
}

namespace PropAttributes {
  static const FString REGISTRY_FORMAT = TEXT(".PropRegistry.json");
  static const FString NAME = TEXT("name");
  static const FString MESH_PATH = TEXT("path");
  static const FString SIZE = TEXT("size");
}

FString UCarlaBlueprintRegistry::PropSizeTypeToString(EPropSize PropSizeType)
{
  const UEnum *ptr = FindObject<UEnum>(ANY_PACKAGE, TEXT("EPropSize"), true);
  if (!ptr)
  {
    return FString("unknown");
  }
  return ptr->GetNameStringByIndex(static_cast<int32>(PropSizeType));
}

EPropSize UCarlaBlueprintRegistry::StringToPropSizeType(FString PropSize)
{
  const UEnum *EnumPtr = FindObject<UEnum>(ANY_PACKAGE, TEXT("EPropSize"), true);
  if (EnumPtr)
  {
    return (EPropSize) EnumPtr->GetIndexByName(FName(*PropSize));
  }
  return EPropSize::INVALID;
}

void UCarlaBlueprintRegistry::AddToCarlaBlueprintRegistry(const TArray<FPropParameters> &PropParametersArray)
{
  // Load default props file
  FString DefaultPropFilePath = CommonAttributes::PATH + CommonAttributes::DEFAULT +
      PropAttributes::REGISTRY_FORMAT;
  FString JsonString;
  FFileHelper::LoadFileToString(JsonString, *DefaultPropFilePath);

  TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
  TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);

  // Get Array of Props and save indexes of each prop in PropIndexes Map
  TMap<FString, int> PropIndexes;
  TArray<TSharedPtr<FJsonValue>> ResultPropJsonArray;
  if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
  {
    ResultPropJsonArray = JsonObject->GetArrayField(CommonAttributes::DEFINITIONS);
    for (auto i = 0u; i < ResultPropJsonArray.Num(); ++i)
    {
      TSharedPtr<FJsonObject> PropJsonObject = ResultPropJsonArray[i]->AsObject();

      FString Name = PropJsonObject->GetStringField(PropAttributes::NAME);

      PropIndexes.Add(Name, i);
    }
  }

  // Add Input Prop or Update Prop if already exists
  for (auto &PropParameter : PropParametersArray)
  {
    TSharedPtr<FJsonObject> PropJsonObject;

    int *PropIndex = PropIndexes.Find(PropParameter.Name);
    if (PropIndex)
    {
      PropJsonObject = ResultPropJsonArray[*PropIndex]->AsObject();
    }
    else
    {
      PropJsonObject = MakeShareable(new FJsonObject);
    }

    // Fill Prop Json
    PropJsonObject->SetStringField(PropAttributes::NAME, PropParameter.Name);
    PropJsonObject->SetStringField(PropAttributes::MESH_PATH, PropParameter.Mesh->GetPathName());

    FString PropSize = PropSizeTypeToString(PropParameter.Size);
    PropJsonObject->SetStringField(PropAttributes::SIZE, PropSize);

    // Add or Update
    TSharedRef<FJsonValue> PropJsonValue = MakeShareable(new FJsonValueObject(PropJsonObject));
    if (PropIndex)
    {
      ResultPropJsonArray[*PropIndex] = PropJsonValue;
    }
    else
    {
      ResultPropJsonArray.Add(PropJsonValue);

      PropIndexes.Add(PropParameter.Name, ResultPropJsonArray.Num() - 1);
    }
  }

  JsonObject->SetArrayField(CommonAttributes::DEFINITIONS, ResultPropJsonArray);

  // Serialize file
  FString OutputString;
  TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
  FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

  // Save file
  FFileHelper::SaveStringToFile(OutputString, *DefaultPropFilePath);

}

void UCarlaBlueprintRegistry::LoadPropDefinitions(TArray<FActorDefinition> &Definitions)
{
  // Loads prop registry json files
  const FString WildCard = FString("*").Append(PropAttributes::REGISTRY_FORMAT);

  TArray<FString> PropFileNames;
  IFileManager::Get().FindFilesRecursive(PropFileNames,
      *CommonAttributes::PATH,
      *WildCard,
      true,
      false,
      false);

  // Sort and place Default File First
  PropFileNames.Sort();
  FString DefaultFileName;
  bool bDefaultFileFound = false;
  for (auto i = 0u; i < PropFileNames.Num() && !bDefaultFileFound; ++i)
  {
    if (PropFileNames[i].Contains(CommonAttributes::DEFAULT))
    {
      DefaultFileName = PropFileNames[i];
      PropFileNames.RemoveAt(i);
      bDefaultFileFound = true;
    }
  }
  if (bDefaultFileFound)
  {
    PropFileNames.Insert(DefaultFileName, 0);
  }

  // Read all registry files and overwrite default registry values with user
  // registry files
  TArray<FPropParameters> PropParametersArray;
  TMap<FString, int> PropIndexes;

  for (auto i = 0u; i < PropFileNames.Num(); ++i)
  {
    FString FileJsonContent;
    if (FFileHelper::LoadFileToString(FileJsonContent, *PropFileNames[i]))
    {
      TSharedPtr<FJsonObject> JsonParsed;
      TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(FileJsonContent);
      if (FJsonSerializer::Deserialize(JsonReader, JsonParsed))
      {
        auto PropJsonArray = JsonParsed->GetArrayField(CommonAttributes::DEFINITIONS);

        for (auto &PropJsonValue : PropJsonArray)
        {
          // Build Prop Parameter from Json
          TSharedPtr<FJsonObject> PropJsonObject = PropJsonValue->AsObject();

          FString PropName = PropJsonObject->GetStringField(PropAttributes::NAME);

          FString PropMeshPath = PropJsonObject->GetStringField(PropAttributes::MESH_PATH);
          UStaticMesh *PropMesh = LoadObject<UStaticMesh>(nullptr, *PropMeshPath);

          EPropSize PropSize = StringToPropSizeType(PropJsonObject->GetStringField(PropAttributes::SIZE));

          FPropParameters Params {PropName, PropMesh, PropSize};

          // Add or Update
          if (PropIndexes.Contains(PropName))
          {
            PropParametersArray[PropIndexes[PropName]] = Params;
          }
          else
          {
            PropParametersArray.Add(Params);
            PropIndexes.Add(Params.Name, PropParametersArray.Num() - 1);
          }
        }
      }
    }
  }

  UActorBlueprintFunctionLibrary::MakePropDefinitions(PropParametersArray, Definitions);
}