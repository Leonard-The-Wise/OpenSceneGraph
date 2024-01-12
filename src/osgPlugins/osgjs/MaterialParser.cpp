
#include "pch.h"
#include "json.hpp"

#include "OsgjsParserHelper.h"
#include "MaterialParser.h"

using namespace osgJSONParser;
using namespace nlohmann;


TextureInfo parseTexture(const json& textureInfoDoc)
{
	TextureInfo returnTexture;

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

static ChannelInfo parseChannel(const json& channelValue)
{
	ChannelInfo returnInfo;

	if (channelValue.contains("enable"))
	{
		returnInfo.Enable = channelValue["enable"].get<bool>();
	}

	if (channelValue.contains("factor"))
	{
		returnInfo.Factor = channelValue["factor"].get<double>();
	}

	if (channelValue.contains("color") && channelValue["color"].is_array())
	{
		returnInfo.Color.resize(3);
		for (int i = 0; i < 3 && i < channelValue["color"].size(); ++i)
		{
			returnInfo.Color[i] = channelValue["color"][i].get<double>();
		}
	}

	if (channelValue.contains("texture") && channelValue["texture"].is_object())
	{
		returnInfo.Texture = parseTexture(channelValue["texture"]);

	}

	return returnInfo;
}

bool MaterialFile::readMaterialFile(const std::string& viewerInfoFileName, const std::string& textureInfoFileName)
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

	return parseTextureInfo(textureInfoDoc);
}

bool MaterialFile::parseViewerInfo(const json& viewerInfoDoc)
{
	if (!viewerInfoDoc.contains("options"))
		return false;

	if (!viewerInfoDoc["options"].is_object())
		return false;

	const json& options = viewerInfoDoc["options"];
	if (options.contains("materials") && options["materials"].is_object())
	{
		const json& materials = options["materials"];

		for (auto& materialItem : materials.items())
		{
			if (materialItem.value().is_object())
			{
				MaterialInfo material;
				std::string materialName;
				auto& itemValue = materialItem.value();
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

				if (itemValue.contains("channels") && itemValue["channels"].is_object())
				{
					for (auto& channel : itemValue["channels"].items())
					{
						if (knownChannelNames.find(channel.key()) != knownChannelNames.end())
						{
							ChannelInfo channelInfo = parseChannel(channel.value());
							material.Channels[channel.key()] = channelInfo;

						}
						else
						{
							OSG_WARN << "WARNING: Unknown material layer name: " << channel.key() << std::endl;
						}
					}
				}

				_materials[materialName] = material;
			}
		}
	}

	return true;
}

bool MaterialFile::parseTextureInfo(const json& textureInfoDoc)
{
	if (!textureInfoDoc.contains("results"))
		return false;

	if (!textureInfoDoc["results"].is_array())
		return false;

	// Recover texture names
	for (auto& texture : textureInfoDoc["results"])
	{
		if (texture.is_object())
		{
			std::string textureName = texture["name"];
			std::string textureUID = texture["uid"];

			for (auto& material : _materials)
			{
				MaterialInfo& materialInfo = material.second;
				for (auto& materialChannel : materialInfo.Channels)
				{
					ChannelInfo& channelInfo = materialChannel.second;
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