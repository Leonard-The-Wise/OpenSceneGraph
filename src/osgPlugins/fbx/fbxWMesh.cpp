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
				const osg::Array* basetexcoords;
				// Get the first texCoord array avaliable
				for (int i = 0; i < 32; i++)
					if (basetexcoords = pGeometry->getTexCoordArray(i))
						break;

				if (basetexcoords && basetexcoords->getNumElements() > 0)
				{
					FbxVector2 texcoord;
					switch (basetexcoords->getType())
					{
					case osg::Array::Vec2ArrayType:
					{
						const osg::Vec2& vec = (*static_cast<const osg::Vec2Array*>(basetexcoords))[vertexIndex];
						texcoord.Set(vec.x(), 1 - vec.y());
						break;
					}
					case osg::Array::Vec2dArrayType:
					{
						const osg::Vec2d& vec = (*static_cast<const osg::Vec2dArray*>(basetexcoords))[vertexIndex];
						texcoord.Set(vec.x(), 1 - vec.y());
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
				const osg::Array* texvec;
				for (int i = 0; i < 32; i++)
					if (texvec = geo->getTexCoordArray(i))
						break;

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

}




