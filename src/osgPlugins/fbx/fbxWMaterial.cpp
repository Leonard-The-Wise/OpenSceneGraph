// -*-c++-*-

/*
 * FBX writer for Open Scene Graph
 *
 * Copyright (C) 2009
 *
 * Writing support added 2009 by Thibault Caporal and Sukender (Benoit Neil - http://sukender.free.fr)
 * Writing Rigging, Textures, Materials and Animations added 2024 by Leonardo Silva (https://github.com/Leonard-The-Wise/)
 *
 * The Open Scene Graph (OSG) is a cross platform C++/OpenGL library for
 * real-time rendering of large 3D photo-realistic models.
 * The OSG homepage is http://www.openscenegraph.org/
 */

#include <climits>                     // required for UINT_MAX
#include <cassert>
#include <osg/CullFace>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osg/PrimitiveSet>
#include <osgDB/FileUtils>
#include <osgDB/WriteFile>

#include <osgAnimation/BasicAnimationManager>
#include <osgAnimation/Animation>
#include <osgAnimation/UpdateBone>
#include <osgAnimation/UpdateMatrixTransform>
#include <osgAnimation/StackedTranslateElement>
#include <osgAnimation/StackedQuaternionElement>
#include <osgAnimation/StackedRotateAxisElement>
#include <osgAnimation/StackedMatrixElement>
#include <osgAnimation/StackedScaleElement>


#include "WriterNodeVisitor.h"

using namespace osg;
using namespace osgAnimation;




namespace pluginfbx
{

	WriterNodeVisitor::MaterialParser::MaterialParser(WriterNodeVisitor& writerNodeVisitor,
		osgDB::ExternalFileWriter& externalWriter,
		const osg::StateSet* stateset,
		const osg::Material* mat,
		const std::vector<const osg::Texture*>& texArray,
		FbxManager* pSdkManager,
		const osgDB::ReaderWriter::Options* options,
		int index) :
		_fbxMaterial(NULL)
	{
		osg::Vec4 diffuse(1, 1, 1, 1),
			ambient(0.2, 0.2, 0.2, 1),
			specular(0, 0, 0, 1),
			emission(1, 1, 1, 1);

		float shininess(0);
		float transparency(0);

		if (mat)
		{
			assert(stateset);
			diffuse = mat->getDiffuse(osg::Material::FRONT);
			ambient = mat->getAmbient(osg::Material::FRONT);
			specular = mat->getSpecular(osg::Material::FRONT);
			shininess = mat->getShininess(osg::Material::FRONT);
			emission = mat->getEmission(osg::Material::FRONT);
			transparency = 1 - diffuse.w();

			const osg::StateAttribute* attribute = stateset->getAttribute(osg::StateAttribute::CULLFACE);
			if (attribute)
			{
				assert(dynamic_cast<const osg::CullFace*>(attribute));
				osg::CullFace::Mode mode = static_cast<const osg::CullFace*>(attribute)->getMode();
				if (mode == osg::CullFace::FRONT)
				{
					OSG_WARN << "FBX Writer: Reversed face (culled FRONT) not supported yet." << std::endl;
				}
				else if (mode != osg::CullFace::BACK)
				{
					assert(mode == osg::CullFace::FRONT_AND_BACK);
					OSG_WARN << "FBX Writer: Invisible face (culled FRONT_AND_BACK) not supported yet." << std::endl;
				}
			}

			_fbxMaterial = FbxSurfacePhong::Create(pSdkManager, mat->getName().c_str());
			if (_fbxMaterial)
			{
				_fbxMaterial->DiffuseFactor.Set(1);
				_fbxMaterial->Diffuse.Set(FbxDouble3(
					diffuse.x(),
					diffuse.y(),
					diffuse.z()));

				_fbxMaterial->TransparencyFactor.Set(transparency);

				_fbxMaterial->Ambient.Set(FbxDouble3(
					ambient.x(),
					ambient.y(),
					ambient.z()));

				_fbxMaterial->Emissive.Set(FbxDouble3(
					emission.x(),
					emission.y(),
					emission.z()));

				_fbxMaterial->Specular.Set(FbxDouble3(
					specular.x(),
					specular.y(),
					specular.z()));

				shininess = shininess < 0 ? 0 : shininess;
				_fbxMaterial->Shininess.Set(shininess);
			}
		}

		if (texArray.size() > 0)
		{
			// Get where on material this texture applies
			for (auto& tex : texArray)
			{
				if (!tex)
					continue;

				MaterialSurfaceLayer textureLayer = getTexMaterialLayer(mat, tex);
				std::string relativePath;

				if (!osgDB::fileExists(tex->getImage(0)->getFileName()))
					externalWriter.write(*tex->getImage(0), options, NULL, &relativePath);
				else
					relativePath = tex->getImage(0)->getFileName();

				FbxFileTexture* fbxTexture = FbxFileTexture::Create(pSdkManager, relativePath.c_str());
				fbxTexture->SetFileName(relativePath.c_str());
				fbxTexture->SetMaterialUse(FbxFileTexture::eModelMaterial);
				fbxTexture->SetMappingType(FbxTexture::eUV);

				// Create a FBX material if needed
				if (!_fbxMaterial)
				{
					_fbxMaterial = FbxSurfacePhong::Create(pSdkManager, relativePath.c_str());
				}

				// Connect texture to material's appropriate channel
				if (_fbxMaterial)
				{
					FbxProperty customProperty;
					switch (textureLayer)
					{
					case MaterialSurfaceLayer::Ambient:
						_fbxMaterial->Ambient.ConnectSrcObject(fbxTexture);
						break;
					case MaterialSurfaceLayer::Diffuse:
						fbxTexture->SetTextureUse(FbxTexture::eStandard);
						_fbxMaterial->Diffuse.ConnectSrcObject(fbxTexture);
						break;
					case MaterialSurfaceLayer::DisplacementColor:
						fbxTexture->SetTextureUse(FbxTexture::eStandard);
						_fbxMaterial->DisplacementColor.ConnectSrcObject(fbxTexture);
						break;
					case MaterialSurfaceLayer::Emissive:
						fbxTexture->SetTextureUse(FbxTexture::eLightMap);
						_fbxMaterial->Emissive.ConnectSrcObject(fbxTexture);
						break;
					case MaterialSurfaceLayer::NormalMap:
						fbxTexture->SetTextureUse(FbxTexture::eBumpNormalMap);
						_fbxMaterial->NormalMap.ConnectSrcObject(fbxTexture);
						break;
					case MaterialSurfaceLayer::Reflection:
						fbxTexture->SetTextureUse(FbxTexture::eSphericalReflectionMap);
						_fbxMaterial->Reflection.ConnectSrcObject(fbxTexture);
						break;
					case MaterialSurfaceLayer::Shininess:
						_fbxMaterial->Shininess.ConnectSrcObject(fbxTexture);
						break;
					case MaterialSurfaceLayer::Specular:
						fbxTexture->SetTextureUse(FbxTexture::eLightMap);
						_fbxMaterial->Specular.ConnectSrcObject(fbxTexture);
						break;
					case MaterialSurfaceLayer::Transparency:
						fbxTexture->SetTextureUse(FbxTexture::eStandard);
						_fbxMaterial->TransparencyFactor.ConnectSrcObject(fbxTexture);
						break;
					}
				}
			}
		}
	}

	// Get texture's material property from UserData if applies
	WriterNodeVisitor::MaterialParser::MaterialSurfaceLayer WriterNodeVisitor::MaterialParser::getTexMaterialLayer(const osg::Material* material, const osg::Texture* texture)
	{
		if (!texture || !material)
			return MaterialSurfaceLayer::None;

		std::string textureFile = texture->getImage(0)->getFileName();
		std::string layerName;

		// Run through all known layer names and try to match textureFile
		for (auto& knownLayer : KnownLayerNames)
		{
			std::string materialFile;
			std::ignore = material->getUserValue(std::string("textureLayer_") + knownLayer, materialFile);
			if (materialFile == textureFile)
			{
				if (knownLayer == "Albedo" || knownLayer == "Diffuse" || knownLayer == "Diffuse colour")
					return MaterialSurfaceLayer::Diffuse;
				else if (knownLayer == "Normal" || knownLayer == "Bump map")
					return MaterialSurfaceLayer::NormalMap;
				else if (knownLayer == "SpecularPBR" || knownLayer == "Specular F0" || knownLayer == "Specular colour" || knownLayer == "Specular hardness" ||
					knownLayer == "Metalness")
					return MaterialSurfaceLayer::Specular;
				else if (knownLayer == "Displacement")
					return MaterialSurfaceLayer::DisplacementColor;
				else if (knownLayer == "Emission")
					return MaterialSurfaceLayer::Emissive;
				else if (knownLayer == "Glossiness" || knownLayer == "Roughness")
					return MaterialSurfaceLayer::Shininess;
				else if (knownLayer == "Opacity")
					return MaterialSurfaceLayer::Transparency;
			}
		}

		return MaterialSurfaceLayer::Diffuse;
	}

	WriterNodeVisitor::MaterialParser* WriterNodeVisitor::processStateSet(const osg::StateSet* ss)
	{
		if (!ss)
			return nullptr;

		const osg::Material* mat = dynamic_cast<const osg::Material*>(ss->getAttribute(osg::StateAttribute::MATERIAL));

		// Look for shared materials between statesets
		if (mat && _materialMap.find(mat) != _materialMap.end())
		{
			return _materialMap.at(mat);
		}

		std::vector<const osg::Texture*> texArray;

		for (unsigned int i = 0; i < ss->getNumTextureAttributeLists(); i++)
		{
			texArray.push_back(dynamic_cast<const osg::Texture*>(ss->getTextureAttribute(i, osg::StateAttribute::TEXTURE)));
		}

		MaterialParser* stateMaterial = new MaterialParser(*this, _externalWriter, ss, mat, texArray, _pSdkManager, _options);

		if (mat)
			_materialMap[mat] = stateMaterial;

		return stateMaterial;
	}
}




