// -*-c++-*-

/*
 * FBX writer for Open Scene Graph
 *
 * Copyright (C) 2009
 *
 * Writing support added 2009 by Thibault Caporal and Sukender (Benoit Neil - http://sukender.free.fr)
 * Writing improvements added 2024 by Leonardo Silva (https://github.com/Leonard-The-Wise/)
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

#include "WriterNodeVisitor.h"

using namespace osg;
using namespace osgAnimation;

// Use namespace qualification to avoid static-link symbol collisions
// from multiply defined symbols.
namespace pluginfbx
{


	/** writes all primitives of a primitive-set out to a stream, decomposes quads to triangles, line-strips to lines etc */
	class PrimitiveIndexWriter : public osg::PrimitiveIndexFunctor
	{
	public:
		PrimitiveIndexWriter(const osg::Geometry* geo,
			ListTriangle& listTriangles,
			unsigned int         drawable_n) :
			_drawable_n(drawable_n),
			_listTriangles(listTriangles),
			_modeCache(0),
			_hasNormalCoords(geo->getNormalArray() != NULL),
			_hasTexCoords(geo->getTexCoordArray(0) != NULL),
			_geo(geo),
			_lastFaceIndex(0),
			_curNormalIndex(0),
			_normalBinding(osg::Geometry::BIND_OFF),
			_mesh(0)
		{
			_normalBinding = geo->getNormalBinding();
			if (!geo->getNormalArray() || geo->getNormalArray()->getNumElements() == 0)
			{
				_normalBinding = osg::Geometry::BIND_OFF;        // Turn off binding if there is no normal data
			}
			reset();
		}

		void reset() { _curNormalIndex = 0; }

		unsigned int getNextFaceIndex() { return _lastFaceIndex; }

		virtual void setVertexArray(unsigned int, const osg::Vec2*) {}

		virtual void setVertexArray(unsigned int, const osg::Vec3*) {}

		virtual void setVertexArray(unsigned int, const osg::Vec4*) {}

		virtual void setVertexArray(unsigned int, const osg::Vec2d*) {}

		virtual void setVertexArray(unsigned int, const osg::Vec3d*) {}

		virtual void setVertexArray(unsigned int, const osg::Vec4d*) {}

		// operator for triangles
		void writeTriangle(unsigned int i1, unsigned int i2, unsigned int i3)
		{
			Triangle triangle;
			triangle.t1 = i1;
			triangle.t2 = i2;
			triangle.t3 = i3;
			if (_normalBinding == osg::Geometry::BIND_PER_VERTEX) {
				triangle.normalIndex1 = i1;
				triangle.normalIndex2 = i2;
				triangle.normalIndex3 = i3;
			}
			else {
				triangle.normalIndex1 = _curNormalIndex;
				triangle.normalIndex2 = _curNormalIndex;
				triangle.normalIndex3 = _curNormalIndex;
			}
			_listTriangles.push_back(std::make_pair(triangle, _drawable_n));
		}

		virtual void begin(GLenum mode)
		{
			_modeCache = mode;
			_indexCache.clear();
		}

		virtual void vertex(unsigned int vert)
		{
			_indexCache.push_back(vert);
		}

		virtual void end()
		{
			if (!_indexCache.empty())
			{
				drawElements(_modeCache, _indexCache.size(), &_indexCache.front());
			}
		}

		virtual void drawArrays(GLenum mode, GLint first, GLsizei count);

		virtual void drawElements(GLenum mode, GLsizei count, const GLubyte* indices)
		{
			drawElementsImplementation<GLubyte>(mode, count, indices);
		}

		virtual void drawElements(GLenum mode, GLsizei count, const GLushort* indices)
		{
			drawElementsImplementation<GLushort>(mode, count, indices);
		}

		virtual void drawElements(GLenum mode, GLsizei count, const GLuint* indices)
		{
			drawElementsImplementation<GLuint>(mode, count, indices);
		}

	protected:
		template <typename T> void drawElementsImplementation(GLenum mode, GLsizei count, const T* indices)
		{
			if (indices == 0 || count == 0) return;

			typedef const T* IndexPointer;

			switch (mode)
			{
			case GL_TRIANGLES:
			{
				IndexPointer ilast = indices + count;
				for (IndexPointer iptr = indices; iptr < ilast; iptr += 3)
				{
					writeTriangle(iptr[0], iptr[1], iptr[2]);
				}
				break;
			}
			case GL_TRIANGLE_STRIP:
			{
				IndexPointer iptr = indices;
				for (GLsizei i = 2; i < count; ++i, ++iptr)
				{
					if (i & 1) writeTriangle(iptr[0], iptr[2], iptr[1]);
					else       writeTriangle(iptr[0], iptr[1], iptr[2]);
				}
				break;
			}
			case GL_QUADS:
			{
				IndexPointer iptr = indices;
				for (GLsizei i = 3; i < count; i += 4, iptr += 4)
				{
					writeTriangle(iptr[0], iptr[1], iptr[2]);
					writeTriangle(iptr[0], iptr[2], iptr[3]);
				}
				break;
			}
			case GL_QUAD_STRIP:
			{
				IndexPointer iptr = indices;
				for (GLsizei i = 3; i < count; i += 2, iptr += 2)
				{
					writeTriangle(iptr[0], iptr[1], iptr[2]);
					writeTriangle(iptr[1], iptr[3], iptr[2]);
				}
				break;
			}
			case GL_POLYGON: // treat polygons as GL_TRIANGLE_FAN
			case GL_TRIANGLE_FAN:
			{
				IndexPointer iptr = indices;
				unsigned int first = *iptr;
				++iptr;
				for (GLsizei i = 2; i < count; ++i, ++iptr)
				{
					writeTriangle(first, iptr[0], iptr[1]);
				}
				break;
			}
			case GL_POINTS:
			case GL_LINES:
			case GL_LINE_STRIP:
			case GL_LINE_LOOP:
				// Not handled
				break;
			default:
				// uhm should never come to this point :)
				break;
			}
			if (_normalBinding == osg::Geometry::BIND_PER_PRIMITIVE_SET) ++_curNormalIndex;
		}

	private:
		PrimitiveIndexWriter& operator = (const PrimitiveIndexWriter&); // { return *this; }

		unsigned int         _drawable_n;
		ListTriangle& _listTriangles;
		GLenum               _modeCache;
		std::vector<GLuint>  _indexCache;
		bool                 _hasNormalCoords, _hasTexCoords;
		const osg::Geometry* _geo;
		unsigned int         _lastFaceIndex;
		unsigned int         _curNormalIndex;
		osg::Geometry::AttributeBinding _normalBinding;
		FbxMesh* _mesh;
	};

	void PrimitiveIndexWriter::drawArrays(GLenum mode, GLint first, GLsizei count)
	{
		unsigned int pos = first;
		switch (mode)
		{
		case GL_TRIANGLES:
			for (GLsizei i = 2; i < count; i += 3, pos += 3)
			{
				writeTriangle(pos, pos + 1, pos + 2);
			}
			break;
		case GL_TRIANGLE_STRIP:
			for (GLsizei i = 2; i < count; ++i, ++pos)
			{
				if (i & 1) writeTriangle(pos, pos + 2, pos + 1);
				else       writeTriangle(pos, pos + 1, pos + 2);
			}
			break;
		case GL_QUADS:
			for (GLsizei i = 3; i < count; i += 4, pos += 4)
			{
				writeTriangle(pos, pos + 1, pos + 2);
				writeTriangle(pos, pos + 2, pos + 3);
			}
			break;
		case GL_QUAD_STRIP:
			for (GLsizei i = 3; i < count; i += 2, pos += 2)
			{
				writeTriangle(pos, pos + 1, pos + 2);
				writeTriangle(pos + 1, pos + 3, pos + 2);
			}
			break;
		case GL_POLYGON: // treat polygons as GL_TRIANGLE_FAN
		case GL_TRIANGLE_FAN:
			pos = first + 1;
			for (GLsizei i = 2; i < count; ++i, ++pos)
			{
				writeTriangle(first, pos, pos + 1);
			}
			break;
		case GL_POINTS:
		case GL_LINES:
		case GL_LINE_STRIP:
		case GL_LINE_LOOP:
		default:
			OSG_WARN << "WriterNodeVisitor :: can't handle mode " << mode << std::endl;
			break;
		}
		if (_normalBinding == osg::Geometry::BIND_PER_PRIMITIVE_SET) ++_curNormalIndex;
	}

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

				_fbxMaterial->Shininess.Set(shininess);
			}
		}

		if (texArray.size() > 0)
		{
			// Get where on material this texture applies
			for (auto& tex : texArray)
			{
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

	unsigned int addPolygon(MapIndices& index_vert, unsigned int vertIndex, unsigned int normIndex, unsigned int drawableNum)
	{
		VertexIndex vert(vertIndex, drawableNum, normIndex);
		MapIndices::iterator itIndex = index_vert.find(vert);
		if (itIndex == index_vert.end())
		{
			unsigned int indexMesh = index_vert.size();
			index_vert.insert(std::make_pair(vert, indexMesh));
			return indexMesh;
		}
		return itIndex->second;
	}

	void addPolygon(FbxMesh* mesh, MapIndices& index_vert, const Triangle& tri, unsigned int drawableNum)
	{
		mesh->AddPolygon(addPolygon(index_vert, tri.t1, tri.normalIndex1, drawableNum));
		mesh->AddPolygon(addPolygon(index_vert, tri.t2, tri.normalIndex2, drawableNum));
		mesh->AddPolygon(addPolygon(index_vert, tri.t3, tri.normalIndex3, drawableNum));
	}


	void WriterNodeVisitor::setControlPointAndNormalsAndUV(const GeometryList& geometryList,
			MapIndices& index_vert,
			bool              texcoords,
			FbxMesh* mesh)
	{
		mesh->InitControlPoints(index_vert.size());
		FbxLayerElementNormal* lLayerElementNormal = FbxLayerElementNormal::Create(mesh, "");
		// For now, FBX writer only supports normals bound per vertices
		lLayerElementNormal->SetMappingMode(FbxLayerElement::eByControlPoint);
		lLayerElementNormal->SetReferenceMode(FbxLayerElement::eDirect);
		lLayerElementNormal->GetDirectArray().SetCount(index_vert.size());
		mesh->GetLayer(0)->SetNormals(lLayerElementNormal);

		FbxLayerElementTangent* lTangentLayer = FbxLayerElementTangent::Create(mesh, "Tangents");
		lTangentLayer->SetMappingMode(FbxLayerElement::eByControlPoint);
		lTangentLayer->SetReferenceMode(FbxLayerElement::eDirect);
		lTangentLayer->GetDirectArray().SetCount(index_vert.size());
		mesh->GetLayer(0)->SetTangents(lTangentLayer);

		FbxLayerElementVertexColor* lVertexColorLayer = FbxLayerElementVertexColor::Create(mesh, "VertexColors");
		lVertexColorLayer->SetMappingMode(FbxLayerElement::eByControlPoint);
		lVertexColorLayer->SetReferenceMode(FbxLayerElement::eDirect);
		lVertexColorLayer->GetDirectArray().SetCount(index_vert.size());
		mesh->GetLayer(0)->SetVertexColors(lVertexColorLayer);

		FbxLayerElementUV* lUVDiffuseLayer = FbxLayerElementUV::Create(mesh, "DiffuseUV");

		if (texcoords)
		{
			lUVDiffuseLayer->SetMappingMode(FbxLayerElement::eByControlPoint);
			lUVDiffuseLayer->SetReferenceMode(FbxLayerElement::eDirect);
			lUVDiffuseLayer->GetDirectArray().SetCount(index_vert.size());
			mesh->GetLayer(0)->SetUVs(lUVDiffuseLayer, FbxLayerElement::eTextureDiffuse);
		}

		for (MapIndices::iterator it = index_vert.begin(); it != index_vert.end(); ++it)
		{
			const osg::Geometry* pGeometry = geometryList[it->first.drawableIndex];
			unsigned int vertexIndex = it->first.vertexIndex;
			unsigned int normalIndex = it->first.normalIndex;

			osg::Matrix rotateMatrix;
			if (dynamic_cast<const RigGeometry*>(pGeometry))
				rotateMatrix.makeRotate(osg::inDegrees(-90.0), osg::X_AXIS); // Fix rigged mesh rotation

			if (!pGeometry)
				continue;

			const osg::Array* basevecs = pGeometry->getVertexArray();
			assert(basevecs);
			if (!basevecs || basevecs->getNumElements() == 0)
			{
				continue;
			}
			FbxVector4 vertex;

			if (vertexIndex >= basevecs->getNumElements())
			{
				OSG_WARN << "FATAL: Found vertex index out of bounds. Try to import model with flag -O disableIndexDecompress (or turn it off if you already enabled it)" << std::endl;
				throw "Exiting without saving.";
			}

			switch (basevecs->getType())
			{
			case osg::Array::Vec4dArrayType:
			{
				const osg::Vec4d& vec = (*static_cast<const osg::Vec4dArray*>(basevecs))[vertexIndex];
				osg::Vec4d vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
				break;
			}
			case osg::Array::Vec4ArrayType:
			{
				const osg::Vec4& vec = (*static_cast<const osg::Vec4Array*>(basevecs))[vertexIndex];
				osg::Vec4 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
				break;
			}
			case osg::Array::Vec4ubArrayType:
			{
				const osg::Vec4ub& vect = (*static_cast<const osg::Vec4ubArray*>(basevecs))[vertexIndex];
				const osg::Vec4 vec(vect.x(), vect.y(), vect.z(), vect.w());
				osg::Vec4 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
				break;
			}
			case osg::Array::Vec4usArrayType:
			{
				const osg::Vec4us& vect = (*static_cast<const osg::Vec4usArray*>(basevecs))[vertexIndex];
				const osg::Vec4 vec(vect.x(), vect.y(), vect.z(), vect.w());
				osg::Vec4 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
				break;
			}
			case osg::Array::Vec4uiArrayType:
			{
				const osg::Vec4ui& vect = (*static_cast<const osg::Vec4uiArray*>(basevecs))[vertexIndex];
				const osg::Vec4 vec(vect.x(), vect.y(), vect.z(), vect.w());
				osg::Vec4 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
				break;
			}
			case osg::Array::Vec4bArrayType:
			{
				const osg::Vec4b& vect = (*static_cast<const osg::Vec4bArray*>(basevecs))[vertexIndex];
				const osg::Vec4 vec(vect.x(), vect.y(), vect.z(), vect.w());
				osg::Vec4 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
				break;
			}
			case osg::Array::Vec4sArrayType:
			{
				const osg::Vec4s& vect = (*static_cast<const osg::Vec4sArray*>(basevecs))[vertexIndex];
				const osg::Vec4 vec(vect.x(), vect.y(), vect.z(), vect.w());
				osg::Vec4 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
				break;
			}
			case osg::Array::Vec4iArrayType:
			{
				const osg::Vec4i& vect = (*static_cast<const osg::Vec4iArray*>(basevecs))[vertexIndex];
				const osg::Vec4 vec(vect.x(), vect.y(), vect.z(), vect.w());
				osg::Vec4 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
				break;
			}

			case osg::Array::Vec3dArrayType:
			{
				const osg::Vec3d& vec = (*static_cast<const osg::Vec3dArray*>(basevecs))[vertexIndex];
				osg::Vec3 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z());
				break;
			}
			case osg::Array::Vec3ArrayType:
			{
				const osg::Vec3& vec = (*static_cast<const osg::Vec3Array*>(basevecs))[vertexIndex];
				osg::Vec3 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z());
				break;
			}
			case osg::Array::Vec3ubArrayType:
			{
				const osg::Vec3ub& vect = (*static_cast<const osg::Vec3ubArray*>(basevecs))[vertexIndex];
				const osg::Vec3 vec(vect.x(), vect.y(), vect.z());
				osg::Vec3 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z());
				break;
			}
			case osg::Array::Vec3usArrayType:
			{
				const osg::Vec3us& vect = (*static_cast<const osg::Vec3usArray*>(basevecs))[vertexIndex];
				const osg::Vec3 vec(vect.x(), vect.y(), vect.z());
				osg::Vec3 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z());
				break;
			}
			case osg::Array::Vec3uiArrayType:
			{
				const osg::Vec3ui& vect = (*static_cast<const osg::Vec3uiArray*>(basevecs))[vertexIndex];
				const osg::Vec3 vec(vect.x(), vect.y(), vect.z());
				osg::Vec3 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z());
				break;
			}
			case osg::Array::Vec3bArrayType:
			{
				const osg::Vec3b& vect = (*static_cast<const osg::Vec3bArray*>(basevecs))[vertexIndex];
				const osg::Vec3 vec(vect.x(), vect.y(), vect.z());
				osg::Vec3 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z());
				break;
			}
			case osg::Array::Vec3sArrayType:
			{
				const osg::Vec3s& vect = (*static_cast<const osg::Vec3sArray*>(basevecs))[vertexIndex];
				const osg::Vec3 vec(vect.x(), vect.y(), vect.z());
				osg::Vec3 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z());
				break;
			}
			case osg::Array::Vec3iArrayType:
			{
				const osg::Vec3i& vect = (*static_cast<const osg::Vec3iArray*>(basevecs))[vertexIndex];
				const osg::Vec3 vec(vect.x(), vect.y(), vect.z());
				osg::Vec3 vecf = vec * rotateMatrix;
				vertex.Set(vecf.x(), vecf.y(), vecf.z());
				break;
			}
			default:
			{
				OSG_NOTIFY(osg::FATAL) << "Error parsing vertex array." << std::endl;
				throw "FATAL: Vertex array is not Vec4 or Vec3. Not implemented.";
			}
			}

			mesh->SetControlPointAt(vertex, it->second);

			const osg::Array* basenormals = pGeometry->getNormalArray();

			if (basenormals && basenormals->getNumElements() > 0)
			{
				FbxVector4 normal;
				bool failed = false;

				switch (basenormals->getType())
				{
				case osg::Array::Vec4ArrayType:
				{
					const osg::Vec4& vec = (*static_cast<const osg::Vec4Array*>(basenormals))[normalIndex];
					osg::Vec4 vecf = vec * rotateMatrix;
					normal.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
					break;
				}
				case osg::Array::Vec4dArrayType:
				{
					const osg::Vec4d& vec = (*static_cast<const osg::Vec4dArray*>(basenormals))[normalIndex];
					osg::Vec4d vecf = vec * rotateMatrix;
					normal.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
					break;
				}
				case osg::Array::Vec3ArrayType:
				{
					const osg::Vec3& vec = (*static_cast<const osg::Vec3Array*>(basenormals))[normalIndex];
					osg::Vec3 vecf = vec * rotateMatrix;
					normal.Set(vecf.x(), vecf.y(), vecf.z());
					break;
				}
				case osg::Array::Vec3dArrayType:
				{
					const osg::Vec3d& vec = (*static_cast<const osg::Vec3dArray*>(basenormals))[normalIndex];
					osg::Vec3d vecf = vec * rotateMatrix;
					normal.Set(vecf.x(), vecf.y(), vecf.z());
					break;
				}
				default:
				{
					OSG_DEBUG << "DEBUG: Error parsing normal array." << std::endl;
					failed = true;
					break;
				}
				}

				if (!failed)
					lLayerElementNormal->GetDirectArray().SetAt(it->second, normal);
			}

			if (texcoords)
			{
				const osg::Array* basetexcoords = pGeometry->getTexCoordArray(0);
				if (basetexcoords && basetexcoords->getNumElements() > 0)
				{
					FbxVector2 texcoord;
					switch (basetexcoords->getType())
					{
					case osg::Array::Vec2ArrayType:
					{
						const osg::Vec2& vec = (*static_cast<const osg::Vec2Array*>(basetexcoords))[vertexIndex];
						texcoord.Set(vec.x(), 1-vec.y());
						break;
					}
					case osg::Array::Vec2dArrayType:
					{
						const osg::Vec2d& vec = (*static_cast<const osg::Vec2dArray*>(basetexcoords))[vertexIndex];
						texcoord.Set(vec.x(), 1-vec.y());
						break;
					}
					default:
					{
						OSG_NOTIFY(osg::FATAL) << "Error parsing texcoord array." << std::endl;
						throw "FATAL: Texture coords array is not Vec2 [floats]. Not implemented";
					}
					}

					lUVDiffuseLayer->GetDirectArray().SetAt(it->second, texcoord);
				}
			}

			const osg::Array* tangents = nullptr;
			for (auto& attrib : pGeometry->getVertexAttribArrayList())
			{
				bool isTangent = false;
				if (attrib->getUserValue("tangent", isTangent))
					if (isTangent)
					{
						tangents = attrib;
						break;
					}
			}

			if (tangents && tangents->getNumElements() > 0)
			{
				FbxVector4 tangent;
				bool failed = false;

				switch (tangents->getType())
				{
				case osg::Array::Vec4ArrayType:
				{
					const osg::Vec4& vec = (*static_cast<const osg::Vec4Array*>(tangents))[vertexIndex];
					osg::Vec4 vecf = vec * rotateMatrix;
					tangent.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
					break;
				}
				case osg::Array::Vec4dArrayType:
				{
					const osg::Vec4d& vec = (*static_cast<const osg::Vec4dArray*>(tangents))[vertexIndex];
					osg::Vec4 vecf = vec * rotateMatrix;
					tangent.Set(vecf.x(), vecf.y(), vecf.z(), vecf.w());
					break;
				}
				case osg::Array::Vec3ArrayType:
				{
					const osg::Vec3& vec = (*static_cast<const osg::Vec3Array*>(tangents))[vertexIndex];
					osg::Vec3 vecf = vec * rotateMatrix;
					tangent.Set(vecf.x(), vecf.y(), vecf.z());
					break;
				}
				case osg::Array::Vec3dArrayType:
				{
					const osg::Vec3d& vec = (*static_cast<const osg::Vec3dArray*>(tangents))[vertexIndex];
					osg::Vec3 vecf = vec * rotateMatrix;
					tangent.Set(vecf.x(), vecf.y(), vecf.z());
					break;
				}
				case osg::Array::Vec2ArrayType:
				{
					const osg::Vec2& vec = (*static_cast<const osg::Vec2Array*>(tangents))[vertexIndex];
					tangent.Set(vec.x(), vec.y(), 0.0);
					break;
				}
				case osg::Array::Vec2dArrayType:
				{
					const osg::Vec2d& vec = (*static_cast<const osg::Vec2dArray*>(tangents))[vertexIndex];
					tangent.Set(vec.x(), vec.y(), 0.0);
					break;
				}
				default:
				{
					OSG_DEBUG << "Error parsing tangent array." << std::endl;
					failed = true;
					break;
				}
				}

				if (!failed)
					lTangentLayer->GetDirectArray().SetAt(it->second, tangent);
			}

			const osg::Array* basecolors = pGeometry->getColorArray();

			if (basecolors && basecolors->getNumElements() > 0)
			{
				FbxVector4 color;
				bool failed = false;

				switch (basecolors->getType())
				{
				case osg::Array::Vec4ArrayType:
				{
					const osg::Vec4& vec = (*static_cast<const osg::Vec4Array*>(basecolors))[vertexIndex];
					color.Set(vec.r(), vec.g(), vec.b(), vec.a());
					break;
				}
				case osg::Array::Vec4dArrayType:
				{
					const osg::Vec4d& vec = (*static_cast<const osg::Vec4dArray*>(basecolors))[vertexIndex];
					color.Set(vec.r(), vec.g(), vec.b(), vec.a());
					break;
				}
				case osg::Array::Vec4ubArrayType:
				{
					const osg::Vec4ub& vec = (*static_cast<const osg::Vec4ubArray*>(basevecs))[vertexIndex];
					color.Set(vec.r() / 255.0, vec.g() / 255.0, vec.b() / 255.0, vec.a() / 255.0);
					break;
				}
				case osg::Array::Vec4bArrayType:
				{
					const osg::Vec4b& vec = (*static_cast<const osg::Vec4bArray*>(basevecs))[vertexIndex];
					color.Set(vec.r() / 255.0, vec.g() / 255.0, vec.b() / 255.0, vec.a() / 255.0);
					break;
				}

				default:
				{
					OSG_DEBUG << "DEBUG: Error parsing color array." << std::endl;
					failed = true;
					break;
				}
				}

				if (!failed)
					lVertexColorLayer->GetDirectArray().SetAt(it->second, color);
			}

		}
	}

	void WriterNodeVisitor::createMorphTargets(const osgAnimation::MorphGeometry* morphGeometry, FbxMesh* mesh, const osg::Matrix& rotateMatrix)
	{
		FbxBlendShape* fbxBlendShape = FbxBlendShape::Create(_pSdkManager, morphGeometry->getName().c_str());
		mesh->AddDeformer(fbxBlendShape);

		for (unsigned int i = 0; i < morphGeometry->getMorphTargetList().size(); ++i)
		{
			const osg::Geometry* osgMorphTarget = morphGeometry->getMorphTarget(i).getGeometry();
			FbxBlendShapeChannel* fbxChannel = FbxBlendShapeChannel::Create(_pSdkManager, osgMorphTarget->getName().c_str());

			std::stringstream ss;
			ss << osgMorphTarget->getName().c_str() << "_" << i;
			FbxShape* fbxShape = FbxShape::Create(_pSdkManager, ss.str().c_str());
			fbxChannel->AddTargetShape(fbxShape);

			// Create vertices
			const osg::Array* vertices = osgMorphTarget->getVertexArray();
			if (!vertices)
				continue;

			fbxShape->InitControlPoints(vertices->getNumElements());
			FbxVector4* fbxControlPoints = fbxShape->GetControlPoints();

			for (unsigned int j = 0; j < vertices->getNumElements(); j++)
			{
				switch (vertices->getType())
				{
				case Array::Vec4dArrayType:
				{
					osg::Vec4d vec = dynamic_cast<const Vec4dArray*>(vertices)->at(j);
					osg::Vec4d vecf = vec * rotateMatrix;
					fbxControlPoints[j] = FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w());
					break;
				}
				case Array::Vec4ArrayType:
				{
					osg::Vec4 vec = dynamic_cast<const Vec4Array*>(vertices)->at(j);
					osg::Vec4d vecf = vec * rotateMatrix;
					fbxControlPoints[j] = FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w());
					break;
				}
				case Array::Vec3dArrayType:
				{
					osg::Vec3d vec = dynamic_cast<const Vec3dArray*>(vertices)->at(j);
					osg::Vec3d vecf = vec * rotateMatrix;
					fbxControlPoints[j] = FbxVector4(vecf.x(), vecf.y(), vecf.z());
					break;
				}
				case Array::Vec3ArrayType:
				{
					osg::Vec3 vec = dynamic_cast<const Vec3Array*>(vertices)->at(j);
					osg::Vec3d vecf = vec * rotateMatrix;
					fbxControlPoints[j] = FbxVector4(vecf.x(), vecf.y(), vecf.z());
					break;
				}
				default:
				{
					OSG_FATAL << "Error creating morph target for FbxMesh " << mesh->GetName() << std::endl;
					throw "Exiting...";
				}
				}
			}

			// Create Normals
			const osg::Array* normals = osgMorphTarget->getNormalArray();

			if (normals)
			{
				FbxLayer* layer = fbxShape->GetLayer(0);
				int layerNum(0);
				if (!layer) {
					layerNum = fbxShape->CreateLayer();
					layer = fbxShape->GetLayer(0);
				}

				FbxLayerElementNormal* layerElementNormal = FbxLayerElementNormal::Create(fbxShape, "Normals");
				layerElementNormal->SetMappingMode(FbxLayerElement::eByControlPoint);
				layerElementNormal->SetReferenceMode(FbxLayerElement::eDirect);
				layerElementNormal->GetDirectArray().SetCount(normals->getNumElements());

				for (unsigned int j = 0; j < normals->getNumElements(); ++j)
				{
					switch (normals->getType())
					{
					case Array::Vec4dArrayType:
					{
						osg::Vec4d vec = dynamic_cast<const Vec4dArray*>(normals)->at(j);
						osg::Vec4d vecf = vec * rotateMatrix;
						layerElementNormal->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w()));
						break;
					}
					case Array::Vec4ArrayType:
					{
						osg::Vec4 vec = dynamic_cast<const Vec4Array*>(normals)->at(j);
						osg::Vec4 vecf = vec * rotateMatrix;
						layerElementNormal->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w()));
						break;
					}
					case Array::Vec3dArrayType:
					{
						osg::Vec3d vec = dynamic_cast<const Vec3dArray*>(normals)->at(j);
						osg::Vec3 vecf = vec * rotateMatrix;
						layerElementNormal->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z()));
						break;
					}
					case Array::Vec3ArrayType:
					{
						osg::Vec3 vec = dynamic_cast<const Vec3Array*>(normals)->at(j);
						osg::Vec3 vecf = vec * rotateMatrix;
						layerElementNormal->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z()));
						break;
					}
					}
				}

				layer->SetNormals(layerElementNormal);
			}

			// Create Colors
			const osg::Array* colors = osgMorphTarget->getColorArray();

			if (colors)
			{
				FbxLayer* layer = fbxShape->GetLayer(0);
				int layerNum(0);
				if (!layer) {
					layerNum = fbxShape->CreateLayer();
					layer = fbxShape->GetLayer(0);
				}

				FbxLayerElementVertexColor* layerVertexColors = FbxLayerElementVertexColor::Create(fbxShape, "VertexColors");
				layerVertexColors->SetMappingMode(FbxLayerElement::eByControlPoint);
				layerVertexColors->SetReferenceMode(FbxLayerElement::eDirect);
				layerVertexColors->GetDirectArray().SetCount(colors->getNumElements());

				for (unsigned int j = 0; j < colors->getNumElements(); ++j)
				{
					switch (colors->getType())
					{
					case Array::Vec4dArrayType:
					{
						osg::Vec4d vec = dynamic_cast<const Vec4dArray*>(colors)->at(j);
						layerVertexColors->GetDirectArray().SetAt(j, FbxVector4(vec.r(), vec.g(), vec.b(), vec.a()));
						break;
					}
					case Array::Vec4ArrayType:
					{
						osg::Vec4 vec = dynamic_cast<const Vec4Array*>(colors)->at(j);
						layerVertexColors->GetDirectArray().SetAt(j, FbxVector4(vec.r(), vec.g(), vec.b(), vec.a()));
						break;
					}
					case Array::Vec4ubArrayType:
					{
						osg::Vec4ub vec = dynamic_cast<const Vec4ubArray*>(colors)->at(j);
						layerVertexColors->GetDirectArray().SetAt(j, FbxVector4(vec.r() / 255.0, vec.g() / 255.0, vec.b() / 255.0, vec.a() / 255.0));
						break;
					}
					case Array::Vec4bArrayType:
					{
						osg::Vec4b vec = dynamic_cast<const Vec4bArray*>(colors)->at(j);
						layerVertexColors->GetDirectArray().SetAt(j, FbxVector4(vec.r() / 255.0, vec.g() / 255.0, vec.b() / 255.0, vec.a() / 255.0));
						break;
					}
					}
				}

				layer->SetVertexColors(layerVertexColors);
			}

			// Create tangents
			const osg::Array* tangents = nullptr;
			for (auto& attrib : osgMorphTarget->getVertexAttribArrayList())
			{
				bool isTangent = false;
				if (attrib->getUserValue("tangent", isTangent))
					if (isTangent)
					{
						tangents = attrib;
						break;
					}
			}

			if (tangents)
			{
				FbxLayer* layer = fbxShape->GetLayer(0);
				int layerNum(0);
				if (!layer) {
					layerNum = fbxShape->CreateLayer();
					layer = fbxShape->GetLayer(0);
				}

				FbxLayerElementTangent* layerTangents = FbxLayerElementTangent::Create(fbxShape, "Tangents");
				layerTangents->SetMappingMode(FbxLayerElement::eByControlPoint);
				layerTangents->SetReferenceMode(FbxLayerElement::eDirect);
				layerTangents->GetDirectArray().SetCount(tangents->getNumElements());

				for (unsigned int j = 0; j < tangents->getNumElements(); ++j)
				{
					switch (tangents->getType())
					{
					case Array::Vec4dArrayType:
					{
						osg::Vec4d vec = dynamic_cast<const Vec4dArray*>(tangents)->at(j);
						osg::Vec4d vecf = vec * rotateMatrix;
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w()));
						break;
					}
					case Array::Vec4ArrayType:
					{
						osg::Vec4 vec = dynamic_cast<const Vec4Array*>(tangents)->at(j);
						osg::Vec4 vecf = vec * rotateMatrix;
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w()));
						break;
					}
					case Array::Vec3dArrayType:
					{
						osg::Vec3d vec = dynamic_cast<const Vec3dArray*>(tangents)->at(j);
						osg::Vec3d vecf = vec * rotateMatrix;
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z()));
						break;
					}
					case Array::Vec3ArrayType:
					{
						osg::Vec3 vec = dynamic_cast<const Vec3Array*>(tangents)->at(j);
						osg::Vec3 vecf = vec * rotateMatrix;
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z()));
						break;
					}
					case Array::Vec2dArrayType:
					{
						osg::Vec2d vec = dynamic_cast<const Vec2dArray*>(tangents)->at(j);
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vec.x(), vec.y(), 0.0));
						break;
					}
					case Array::Vec2ArrayType:
					{
						osg::Vec2 vec = dynamic_cast<const Vec2Array*>(tangents)->at(j);
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vec.x(), vec.y(), 0.0));
						break;
					}
					}
				}

				layer->SetTangents(layerTangents);
			}

			// Create TexCoords
			const osg::Array* texCoord = osgMorphTarget->getTexCoordArray(0);

			if (texCoord)
			{
				FbxLayer* layer = fbxShape->GetLayer(0);
				int layerNum(0);
				if (!layer) {
					layerNum = fbxShape->CreateLayer();
					layer = fbxShape->GetLayer(0);
				}

				FbxLayerElementUV* layerDiffuseUV = FbxLayerElementUV::Create(fbxShape, "UVDiffuse");
				layerDiffuseUV->SetMappingMode(FbxLayerElement::eByControlPoint);
				layerDiffuseUV->SetReferenceMode(FbxLayerElement::eDirect);
				layerDiffuseUV->GetDirectArray().SetCount(texCoord->getNumElements());

				for (unsigned int j = 0; j < texCoord->getNumElements(); ++j)
				{
					switch (texCoord->getType())
					{
					case Array::Vec2dArrayType:
					{
						osg::Vec2d vec = dynamic_cast<const Vec2dArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2ArrayType:
					{
						osg::Vec2 vec = dynamic_cast<const Vec2Array*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2ubArrayType:
					{
						osg::Vec2ub vec = dynamic_cast<const Vec2ubArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2usArrayType:
					{
						osg::Vec2us vec = dynamic_cast<const Vec2usArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2uiArrayType:
					{
						osg::Vec2ui vec = dynamic_cast<const Vec2uiArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2bArrayType:
					{
						osg::Vec2b vec = dynamic_cast<const Vec2bArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2sArrayType:
					{
						osg::Vec2s vec = dynamic_cast<const Vec2sArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2iArrayType:
					{
						osg::Vec2i vec = dynamic_cast<const Vec2iArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					}
				}

				layer->SetUVs(layerDiffuseUV);
			}

		}
	}

	void WriterNodeVisitor::buildMesh(const std::string& name,
		const GeometryList& geometryList,
		ListTriangle& listTriangles,
		bool              texcoords,
		const MaterialParser& materialParser)
	{
		MapIndices index_vert;
		FbxMesh* mesh = FbxMesh::Create(_pSdkManager, name.c_str());
		_meshList.push_back(mesh);

		_curFbxNode->AddNodeAttribute(mesh);
		_curFbxNode->SetShadingMode(FbxNode::eTextureShading);
		FbxLayer* lLayer = mesh->GetLayer(0);
		if (lLayer == NULL)
		{
			mesh->CreateLayer();
			lLayer = mesh->GetLayer(0);
		}

		unsigned int i = 0;
		for (ListTriangle::iterator it = listTriangles.begin(); it != listTriangles.end(); ++it, ++i) //Go through the triangle list to define meshs
		{
			mesh->BeginPolygon();
			addPolygon(mesh, index_vert, it->first, it->second);
			mesh->EndPolygon();
		}

		// Build vertices and recalculate normals and tangents
		setControlPointAndNormalsAndUV(geometryList, index_vert, texcoords, mesh);
		mesh->GenerateNormals(true);
		mesh->GenerateTangentsDataForAllUVSets(true);

		FbxSurfacePhong* meshMaterial = materialParser.getFbxMaterial();
		if (meshMaterial)
			_curFbxNode->AddMaterial(meshMaterial);

		// Since we changed our geometryList to contain only 1 geometry (or Rig or Morph), we can safely pick the first Morph and process
		// Might need to change this approach in the future.
		osg::Matrix rotateMatrix;
		const osgAnimation::MorphGeometry* morph = dynamic_cast<const osgAnimation::MorphGeometry*>(geometryList[0]);
		if (morph)
			createMorphTargets(morph, mesh, rotateMatrix);

		// Look for morph geometries inside rig
		const osgAnimation::RigGeometry* rig = dynamic_cast<const osgAnimation::RigGeometry*>(geometryList[0]);
		if (rig)
		{
			const osgAnimation::MorphGeometry* rigMorph = dynamic_cast<const osgAnimation::MorphGeometry*>(rig->getSourceGeometry());
			rotateMatrix.makeRotate(osg::inDegrees(-90.0), osg::X_AXIS); // Fix rigged mesh rotation
			if (rigMorph)
				createMorphTargets(rigMorph, mesh, rotateMatrix);
		}

		_geometryList.clear();
		_listTriangles.clear();
		_texcoords = false;
		_drawableNum = 0;
	}


	void WriterNodeVisitor::applySkinning(const osgAnimation::VertexInfluenceMap& vim, FbxMesh* fbxMesh)
	{
		FbxSkin* skinDeformer = FbxSkin::Create(_pSdkManager, "");

		for (const auto& influence : vim)
		{
			const std::string& boneName = influence.first;

			BonePair bonePair;
			if (_boneNodeMap.find(boneName) != _boneNodeMap.end())
				bonePair = _boneNodeMap.at(boneName);

			ref_ptr<osgAnimation::Bone> bone = bonePair.first;
			FbxNode* fbxBoneNode = bonePair.second;

			if (!bone)
			{
				OSG_WARN << "WARNING: FBX Mesh " << fbxMesh->GetName() << " has a missing bone: " << boneName << std::endl;
				continue;
			}

			std::stringstream clusterName;
			clusterName << bone->getName() << "_cluster";
			FbxCluster* cluster = FbxCluster::Create(_pSdkManager, clusterName.str().c_str());
			cluster->SetLink(fbxBoneNode);
			cluster->SetLinkMode(FbxCluster::eNormalize);

			for (const auto& weightPair : influence.second)
			{
				int vertexIndex = weightPair.first;
				double weight = weightPair.second;
				cluster->AddControlPointIndex(vertexIndex, weight);
			}

			skinDeformer->AddCluster(cluster);

			osg::Matrixd osgInvBindMatrix = Matrix::inverse(bone->getInvBindMatrixInSkeletonSpace());

			FbxAMatrix fbxInvBindMatrix;
			for (int row = 0; row < 4; ++row) {
				for (int col = 0; col < 4; ++col) {
					fbxInvBindMatrix[row][col] = osgInvBindMatrix(row, col);
				}
			}

			cluster->SetTransformLinkMatrix(fbxInvBindMatrix);
		}

		fbxMesh->AddDeformer(skinDeformer);
	}

	void WriterNodeVisitor::buildMeshSkin()
	{
		for (auto& entry : _riggedMeshMap)
		{
			const osgAnimation::VertexInfluenceMap* vim = entry.first->getInfluenceMap();
			if (!vim)
				continue;

			FbxNode* meshNode = entry.second;
			FbxMesh* mesh = nullptr;

			int attributeCount = meshNode->GetNodeAttributeCount();

			for (int index = 0; index < attributeCount; index++) {
				FbxNodeAttribute* attribute = meshNode->GetNodeAttributeByIndex(index);

				if (attribute && attribute->GetAttributeType() == FbxNodeAttribute::eMesh) {
					mesh = FbxCast<FbxMesh>(attribute);
					break;
				}
			}

			if (mesh)
				applySkinning(*vim, mesh);
		}
	}

	void WriterNodeVisitor::createListTriangle(const osg::Geometry* geo,
		ListTriangle& listTriangles,
		bool& texcoords,
		unsigned int         drawable_n)
	{
		unsigned int nbVertices = 0;
		{
			const osg::Array* vecs = geo->getVertexArray();
			if (vecs)
			{
				nbVertices = vecs->getNumElements();

				// Texture coords
				const osg::Array* texvec = geo->getTexCoordArray(0);
				if (texvec)
				{
					unsigned int nb = texvec->getNumElements();
					if (nb == nbVertices) texcoords = true;
					else
					{
						OSG_WARN << "There are more/less texture coords than vertices! Ignoring texture coords.";
					}
				}
			}
		}

		if (nbVertices == 0) return;

		PrimitiveIndexWriter pif(geo, listTriangles, drawable_n);
		for (unsigned int iPrimSet = 0; iPrimSet < geo->getNumPrimitiveSets(); ++iPrimSet) //Fill the Triangle List
		{
			const osg::PrimitiveSet* ps = geo->getPrimitiveSet(iPrimSet);
			const_cast<osg::PrimitiveSet*>(ps)->accept(pif);
		}
	}

	void WriterNodeVisitor::apply(osg::Geometry& geometry)
	{
		ref_ptr<RigGeometry> rigGeometry = dynamic_cast<RigGeometry*>(&geometry);
		ref_ptr<MorphGeometry> morphGeometry = dynamic_cast<MorphGeometry*>(&geometry);
		const ref_ptr<Group> geoParent = geometry.getParent(0);

		if (rigGeometry)
		{
			rigGeometry->copyFrom(*rigGeometry->getSourceGeometry());

			if (rigGeometry->getName().empty())
				rigGeometry->setName(rigGeometry->getSourceGeometry()->getName());
		}

		// retrieved from the geometry.
		_geometryList.push_back(&geometry);
		createListTriangle(&geometry, _listTriangles, _texcoords, _drawableNum++);

		osg::NodeVisitor::traverse(geometry);

		if (_listTriangles.size() > 0)
		{
			FbxNode* parent = _curFbxNode;

			FbxNode* nodeFBX = FbxNode::Create(_pSdkManager, geometry.getName().empty() ? "DefaultMesh" : geometry.getName().c_str());
			_curFbxNode->AddChild(nodeFBX);
			_curFbxNode = nodeFBX;

			MaterialParser* materialParser = processStateSet(geometry.getStateSet());

			buildMesh(geometry.getName(), _geometryList, _listTriangles, _texcoords, *materialParser);

			if (rigGeometry)
				_riggedMeshMap.emplace(rigGeometry, nodeFBX);
			else if (morphGeometry)
				_MorphedMeshMap.emplace(morphGeometry, nodeFBX);

			_curFbxNode = parent;
		}
	}

	void WriterNodeVisitor::apply(osg::Group& node)
	{
		std::string defaultName;
		if (dynamic_cast<Geode*>(&node))
			defaultName = "DefaultGeode";
		else
			defaultName = "DefaultGroupNode";

		if (_firstNodeProcessed)
		{
			FbxNode* parent = _curFbxNode;

			FbxNode* nodeFBX = FbxNode::Create(_pSdkManager, node.getName().empty() ? defaultName.c_str() : node.getName().c_str());
			_curFbxNode->AddChild(nodeFBX);
			_curFbxNode = nodeFBX;

			traverse(node);

			_curFbxNode = parent;
		}
		else
		{
			//ignore the root node to maintain same hierarchy
			_firstNodeProcessed = true;
			traverse(node);

			// Build Mesh Skins.
			buildMeshSkin();
		}
	}

	void WriterNodeVisitor::apply(osg::MatrixTransform& node)
	{

		std::string nodeName;
		ref_ptr<Skeleton> skeleton = dynamic_cast<Skeleton*>(&node);
		ref_ptr<Bone> bone = dynamic_cast<Bone*>(&node);

		if (skeleton)
			nodeName = node.getName().empty() ? "DefaultSkeleton" : node.getName();
		else if (bone)
			nodeName = node.getName().empty() ? "DefaultBone" : node.getName();
		else
			nodeName = node.getName().empty() ? "DefaultTransform" : node.getName();

		FbxNode* parent = _curFbxNode;
		_curFbxNode = FbxNode::Create(_pSdkManager, nodeName.c_str());
		parent->AddChild(_curFbxNode);

		// Get custom parameter on node to first Matrix. If we are the first, also save the current transform
		// for later (_firstMatrixPostProcess)
		std::string firstMatrix;
		const DefaultUserDataContainer* udc = dynamic_cast<DefaultUserDataContainer*>(node.getUserDataContainer());
		if (udc && udc->getUserValue("firstMatrix", firstMatrix))
		{
			std::vector<double> values;
			std::stringstream ss(firstMatrix);
			std::string item;

			while (std::getline(ss, item, ',')) {
				values.push_back(std::stod(item));
			}

			if (values.size() != 16) {
				throw std::runtime_error("Incorrect number of elements to osg::Matrix!");
			}

			for (int i = 0; i < 16; ++i) {
				_firstMatrix(i / 4, i % 4) = values[i];
			}
		}

		// Process Skeleton and Bones
		if (skeleton || bone)
		{
			FbxSkeleton* fbxSkel = FbxSkeleton::Create(_curFbxNode, nodeName.c_str());
			fbxSkel->SetSkeletonType(skeleton ? FbxSkeleton::eRoot : FbxSkeleton::eLimbNode);
			_curFbxNode->SetNodeAttribute(fbxSkel);

			if (bone)
				_boneNodeMap.emplace(nodeName, std::make_pair(bone, _curFbxNode));
		}

		// Set transforms for node
		osg::Matrix matrix = node.getMatrix();

		osg::Vec3d pos, scl;
		osg::Quat rot, so;

		matrix.decompose(pos, rot, scl, so);
		_curFbxNode->LclTranslation.Set(FbxDouble3(pos.x(), pos.y(), pos.z()));
		_curFbxNode->LclScaling.Set(FbxDouble3(scl.x(), scl.y(), scl.z()));

		FbxAMatrix mat;

		FbxQuaternion q(rot.x(), rot.y(), rot.z(), rot.w());
		mat.SetQ(q);
		FbxVector4 vec4 = mat.GetR();

		_curFbxNode->LclRotation.Set(FbxDouble3(vec4[0], vec4[1], vec4[2]));

		traverse(node);

		_curFbxNode = parent;
	}

	// end namespace pluginfbx
}
