
#include "pch.h"
#include <unordered_map>

#include "json.hpp"

#include "MaterialParser.h"

const std::string COMMENT = R"(^\/\/.+)";
const std::string MESHNAME = R"(^Mesh \"(?'MeshName'\w+)\" uses material \"(?'MaterialName'\w+)\" and has UniqueID \"(?'UniqueID'\d+)\")";
const std::string MATERIALNAME = R"(^Material \"(?'MaterialName'\w+)\" has ID (?'ID'[\w-]+))";
const std::string MATERIALLINE = R"(^\t(?'TextureLayerName'[\w\s]*?)(\s*+(\((?'FlipAxis'Flipped\s*\w+)\)))?(\s*+(\((?'TexCoord'UV\d+)\)))?(\s*+(\((?'Parameter'[\w\s\d=,]*)\)))*+:\s(?'FileOrParam'[\w.,+-|()]*))";

using namespace osgJSONParser;
using namespace nlohmann;

static std::string stripAllExtensions(const std::string& filename)
{
	std::string finalName = filename;
	while (!osgDB::getFileExtension(finalName).empty())
	{
		std::string ext = osgDB::getFileExtension(finalName);

		// Only remove known extensions
		if (ext != "png" && ext != "gz" && ext != "bin" && ext != "binz" && ext != "zip" && ext != "bmp" && ext != "tiff" && ext != "tga" && ext != "jpg" && ext != "jpeg"
			&& ext != "gif" && ext != "tgz" && ext != "pic" && ext != "pnm" && ext != "dds")
			break;

		finalName = osgDB::getStrippedName(finalName);
	}

	return finalName;
}


TextureInfo2 parseTexture(const json& textureInfoDoc)
{
	TextureInfo2 returnTexture;

	if (textureInfoDoc.contains("uid"))
		returnTexture.UID = textureInfoDoc["uid"].get<std::string>();

	if (textureInfoDoc.contains("wrapS"))
		returnTexture.WrapS = textureInfoDoc["wrapS"].get<std::string>();

	if (textureInfoDoc.contains("wrapT"))
		returnTexture.WrapT = textureInfoDoc["wrapT"].get<std::string>();

	if (textureInfoDoc.contains("magFilter"))
		returnTexture.MagFilter = textureInfoDoc["magFilter"].get<std::string>();

	if (textureInfoDoc.contains("minFilter"))
		returnTexture.MinFilter = textureInfoDoc["minFilter"].get<std::string>();

	if (textureInfoDoc.contains("texCoordUnit"))
		returnTexture.TexCoordUnit = textureInfoDoc["texCoordUnit"].get<int>();

	if (textureInfoDoc.contains("textureTarget"))
		returnTexture.TextureTarget = textureInfoDoc["textureTarget"].get<std::string>();

	if (textureInfoDoc.contains("internalFormat"))
		returnTexture.InternalFormat = textureInfoDoc["internalFormat"].get<std::string>();

	return returnTexture;
}

static ChannelInfo2 parseChannel(const json& channelValue)
{
	ChannelInfo2 returnInfo;

	if (channelValue.contains("enable"))
	{
		returnInfo.Enable = channelValue["enable"].get<bool>();
	}

	if (channelValue.contains("factor"))
	{
		returnInfo.Factor = channelValue["factor"].get<double>();
	}

	if (channelValue.contains("direction"))
	{
		returnInfo.Direction = channelValue["direction"].get<double>();
	}

	if (channelValue.contains("rotation"))
	{
		returnInfo.Rotation = channelValue["rotation"].get<double>();
	}

	if (channelValue.contains("thickness"))
	{
		returnInfo.Thickness = channelValue["thickness"].get<double>();
	}

	if (channelValue.contains("type"))
	{
		returnInfo.Type = channelValue["type"].get<std::string>();
	}

	if (channelValue.contains("thinLayer"))
	{
		returnInfo.ThinLayer = channelValue["thinLayer"].get<bool>();
	}

	if (channelValue.contains("roughnessFactor"))
	{
		returnInfo.RoughnessFactor = channelValue["roughnessFactor"].get<double>();
	}

	if (channelValue.contains("color") && channelValue["color"].is_array())
	{
		returnInfo.Color.resize(3);
		for (int i = 0; i < 3 && i < channelValue["color"].size(); ++i)
		{
			returnInfo.Color[i] = channelValue["color"][i].get<double>();
		}
	}

	if (channelValue.contains("tint") && channelValue["tint"].is_array())
	{
		returnInfo.Tint.resize(3);
		for (int i = 0; i < 3 && i < channelValue["tint"].size(); ++i)
		{
			returnInfo.Tint[i] = channelValue["tint"][i].get<double>();
		}
	}

	if (channelValue.contains("colorFactor") && channelValue["colorFactor"].is_array())
	{
		returnInfo.ColorFactor.resize(3);
		for (int i = 0; i < 3 && i < channelValue["colorFactor"].size(); ++i)
		{
			returnInfo.ColorFactor[i] = channelValue["colorFactor"][i].get<double>();
		}
	}

	if (channelValue.contains("ior"))
	{
		returnInfo.IOR = channelValue["ior"].get<double>();
	}

	if (channelValue.contains("texture") && channelValue["texture"].is_object())
	{
		returnInfo.Texture = parseTexture(channelValue["texture"]);

	}

	return returnInfo;
}

bool MaterialFile2::readMaterialFile(const std::string& viewerInfoFileName, const std::string& textureInfoFileName)
{

	std::string viewerInfoName = osgDB::findDataFile(viewerInfoFileName);
	std::string textureInfoName = osgDB::findDataFile(textureInfoFileName);

	if (viewerInfoName.empty() || textureInfoName.empty())
		return false;

	std::ifstream viewerStream(viewerInfoName.c_str());
	std::ifstream textureStream(textureInfoName.c_str());
	json viewerInfoDoc;
	json textureInfoDoc;

	if (!viewerStream.is_open() || !textureStream.is_open())
		return false;

	viewerStream >> viewerInfoDoc;
	textureStream >> textureInfoDoc;

	if (!parseViewerInfo(viewerInfoDoc))
		return false;

	if (!parseTextureInfo(textureInfoDoc))
		return false;

	makeTextureMap();

	return true;
}

bool MaterialFile2::parseViewerInfo(const json& viewerInfoDoc)
{
	if (!viewerInfoDoc.contains("options"))
		return false;

	if (!viewerInfoDoc["options"].is_object())
		return false;

	const json& options = viewerInfoDoc["options"];

	bool usePBR = false;

	if (options.contains("shading") && options["shading"].is_object())
	{
		const json& shading = options["shading"];
		if (shading.contains("renderer") && shading["renderer"].is_string())
		{
			usePBR = (shading["renderer"].get<std::string>() == "pbr");
		}
	}

	if (options.contains("materials") && options["materials"].is_object())
	{
		const json& materials = options["materials"];

		for (auto& materialItem : materials.items())
		{
			if (materialItem.value().is_object())
			{
				MaterialInfo2 material;
				std::string materialName;
				auto& itemValue = materialItem.value();
				material.UsePBR = usePBR;

				if (itemValue.contains("name"))
				{
					materialName = itemValue["name"].get<std::string>();
					material.Name = materialName;
				}
				else
					return false;

				if (itemValue.contains("version"))
					material.Version = itemValue["version"].get<int>();

				if (itemValue.contains("id"))
					material.ID = itemValue["id"].get<std::string>();

				if (itemValue.contains("stateSetID"))
					material.StateSetID = itemValue["stateSetID"].get<int>();

				if (itemValue.contains("channels") && itemValue["channels"].is_object())
				{
					for (auto& channel : itemValue["channels"].items())
					{
						if (knownChannelNames.find(channel.key()) != knownChannelNames.end())
						{
							ChannelInfo2 channelInfo = parseChannel(channel.value());
							material.Channels[channel.key()] = channelInfo;

						}
						else
						{
							OSG_WARN << "WARNING: Unknown material layer name: " << channel.key() << std::endl;
						}
					}
				}

				if (itemValue.contains("cullFace") && itemValue["cullFace"].is_string())
				{
					std::string cullMode = itemValue["cullFace"].get<std::string>();
					if (cullMode == "BACK")
						material.BackfaceCull = true;
					else
						material.BackfaceCull = false;
				}

				_materials[materialName] = material;
				_stateSetIDMaterial[material.StateSetID] = materialName;
			}
		}
	}

	return true;
}

bool MaterialFile2::parseTextureInfo(const json& textureInfoDoc)
{
	if (!textureInfoDoc.contains("results"))
		return false;

	if (!textureInfoDoc["results"].is_array())
		return false;

	// Recover texture names
	std::set<std::string> knownNames;

	for (auto& texture : textureInfoDoc["results"])
	{
		if (texture.is_object())
		{
			std::string textureName = stripAllExtensions(texture["name"]) + ".png";
			std::string textureUID = texture["uid"];

			// Look for texture name in known names and replace name if found
			std::string tmpTexName = textureName;
			int i = 1;
			while (knownNames.find(tmpTexName) != knownNames.end())
			{
				tmpTexName = stripAllExtensions(textureName) + "." + std::to_string(i) + ".png";
				++i;
			}
			textureName = tmpTexName;
			knownNames.emplace(textureName);

			for (auto& material : _materials)
			{
				MaterialInfo2& materialInfo = material.second;
				for (auto& materialChannel : materialInfo.Channels)
				{
					ChannelInfo2& channelInfo = materialChannel.second;
					if (channelInfo.Texture.UID == textureUID)
					{
						channelInfo.Texture.Name = textureName;
					}
				}
			}
		}
	}

	return true;;
}

void osgJSONParser::MaterialFile2::renameTexture(const std::string& originalFile, const std::string& modifiedFile)
{
	// Rename texture on material map
	for (auto& material : _materials)
	{
		// Run through all materials and rename matching textures.
		MaterialInfo2& mInfo = material.second;
		for (auto& channel : mInfo.Channels)
		{
			// Make a copy
			if (channel.second.Texture.Name == originalFile)
			{
				TextureInfo2 texture = channel.second.Texture;
				texture.Name = modifiedFile;
				channel.second.Texture = texture;
			}
		}
	}

	// Rename texture on texture map
	if (_textureMap.find(originalFile) != _textureMap.end())
	{
		TextureInfo2 newTexture = _textureMap[originalFile];
		_textureMap[modifiedFile] = newTexture;
		_textureMap.erase(originalFile);
	}


}

void MaterialFile2::makeTextureMap()
{
	for (auto& material : _materials)
	{
		// Get all textures.
		MaterialInfo2& mInfo = material.second;
		for (auto& channel : mInfo.Channels)
		{
			if (!channel.second.Enable)
				continue;

			TextureInfo2& texture = channel.second.Texture;
			if (!texture.Name.empty())
				_textureMap[texture.Name] = texture;
		}
	}
}