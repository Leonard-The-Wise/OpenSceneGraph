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

	void WriterNodeVisitor::setControlPointAndNormalsAndUV(MapIndices& index_vert, FbxMesh* mesh, osg::Matrix& rotateMatrix)
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

		if (_texcoords)
		{
			lUVDiffuseLayer->SetMappingMode(FbxLayerElement::eByControlPoint);
			lUVDiffuseLayer->SetReferenceMode(FbxLayerElement::eDirect);
			lUVDiffuseLayer->GetDirectArray().SetCount(index_vert.size());
			mesh->GetLayer(0)->SetUVs(lUVDiffuseLayer, FbxLayerElement::eTextureDiffuse);
		}


		std::vector<bool> failNotify(4, false); // Emits only 1 warning for entire arrays

		for (MapIndices::iterator it = index_vert.begin(); it != index_vert.end(); ++it)
		{
			const osg::Geometry* pGeometry = _geometryList[it->first.drawableIndex];
			std::string geometryName = pGeometry->getName();
			unsigned int vertexIndex = it->first.vertexIndex;
			unsigned int normalIndex = it->first.normalIndex;

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
				OSG_WARN << "FATAL: Found vertex index out of bounds. Try to import model with flag -O disableIndexDecompress (or turn it off if you already enabled it)."
					<< "[Geometry: " << geometryName << "]" << std::endl;
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
				OSG_NOTIFY(osg::FATAL) << "Error parsing vertex array. " << "[Geometry: " << geometryName << "]" << std::endl;
				throw "FATAL: Vertex array is not Vec4 or Vec3. Exiting without saving." ;
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
					if (!failNotify[0])
						OSG_DEBUG << "DEBUG: Error parsing normal array. Normals ignored. " << "[Geometry: " << geometryName << "]" << std::endl;
					failed = true;
					failNotify[0] = true;
					break;
				}
				}

				if (!failed)
					lLayerElementNormal->GetDirectArray().SetAt(it->second, normal);
			}

			if (_texcoords)
			{
				const osg::Array* basetexcoords;
				// Get the first texCoord array avaliable
				for (int i = 0; i < 32; i++)
					if (basetexcoords = pGeometry->getTexCoordArray(i))
						break;

				bool failed = false;
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
						if (!failNotify[1])
							OSG_WARN << "WARNING: Error parsing UVs array. UVs Ignored. " << "[Geometry: " << geometryName << "]" << std::endl;
						failed = true;
						failNotify[1] = true;
					}
					}

					if (!failed)
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
					if (!failNotify[2])
						OSG_DEBUG << "DEBUG: Error parsing tangent array. Tangents ignored. " << "[Geometry: " << geometryName << "]" << std::endl;
					failed = true;
					failNotify[2] = true;
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
					if (!failNotify[3])
						OSG_WARN << "WARNING: Error parsing color array. Colors ignored. " << "[Geometry: " << geometryName << "]" << std::endl;
					failed = true;
					failNotify[3] = true;
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

	static osg::Matrix buildParentMatrixes(const osg::Node& object)
	{
		osg::Matrix mult;
		if (object.getNumParents() > 0)
		{
			mult = buildParentMatrixes(*object.getParent(0));
		}

		if (auto matrixObj = dynamic_cast<const osg::MatrixTransform*>(&object))
		{
			return mult * matrixObj->getMatrix();
		}

		return mult;
	}

	static void snapMeshToParent(const osg::Geometry& geometry, FbxNode* meshNode)
	{
		osg::Matrix matrix = buildParentMatrixes(geometry);

		osg::Vec3d pos, scl;
		osg::Quat rot, so;

		matrix.decompose(pos, rot, scl, so);
		meshNode->LclTranslation.Set(FbxDouble3(pos.x(), pos.y(), pos.z()));
		meshNode->LclScaling.Set(FbxDouble3(scl.x(), scl.y(), scl.z()));

		FbxAMatrix mat;

		FbxQuaternion q(rot.x(), rot.y(), rot.z(), rot.w());
		mat.SetQ(q);
		FbxVector4 vec4 = mat.GetR();

		meshNode->LclRotation.Set(FbxDouble3(vec4[0], vec4[1], vec4[2]));
	}

	FbxNode* WriterNodeVisitor::buildMesh(const osg::Geometry& geometry, const MaterialParser* materialParser)
	{
		// Create a node for this mesh and apply it to Mesh Root
		std::string meshName = geometry.getName();
		FbxNode* meshNode = FbxNode::Create(_pSdkManager, meshName.c_str());
		_MeshesRoot->AddChild(meshNode);

		if (_snapMeshesToParentGroup)
		{
			snapMeshToParent(geometry, meshNode);
		}

		FbxMesh* mesh = FbxMesh::Create(_pSdkManager, meshName.c_str());
		_meshList.push_back(mesh);

		meshNode->AddNodeAttribute(mesh);
		meshNode->SetShadingMode(FbxNode::eTextureShading);
		FbxLayer* lLayer = mesh->GetLayer(0);
		if (lLayer == NULL)
		{
			mesh->CreateLayer();
			lLayer = mesh->GetLayer(0);
		}

		unsigned int i = 0;
		MapIndices index_vert;
		for (ListTriangle::iterator it = _listTriangles.begin(); it != _listTriangles.end(); ++it, ++i) //Go through the triangle list to define meshs
		{
			mesh->BeginPolygon();
			addPolygon(mesh, index_vert, it->first, it->second);
			mesh->EndPolygon();
		}

		// Option to rotate rigged and morphed meshes -180º on X axis
		osg::Matrix rotateMatrix;
		if (_rotateXAxis && (dynamic_cast<const RigGeometry*>(&geometry) || dynamic_cast<const MorphGeometry*>(&geometry)))
		{
			rotateMatrix.makeRotate(osg::inDegrees(-180.0), osg::X_AXIS); // Fix rigged mesh rotation
		}

		// Build vertices, normals, tangents, texcoords, etc. [and recalculate normals and tangents because right now we can't decode them]
		setControlPointAndNormalsAndUV(index_vert, mesh, rotateMatrix);
		mesh->GenerateNormals(true);
		mesh->GenerateTangentsDataForAllUVSets(true);

		if (materialParser)
		{
			FbxSurfacePhong* meshMaterial = materialParser->getFbxMaterial();
			if (meshMaterial)
				meshNode->AddMaterial(meshMaterial);
		}

		// Process morphed geometry
		const osgAnimation::MorphGeometry* morph = dynamic_cast<const osgAnimation::MorphGeometry*>(&geometry);
		if (morph)
			createMorphTargets(morph, mesh, rotateMatrix);

		// Look for morph geometries inside rig
		const osgAnimation::RigGeometry* rig = dynamic_cast<const osgAnimation::RigGeometry*>(&geometry);
		if (rig)
		{
			const osgAnimation::MorphGeometry* rigMorph = dynamic_cast<const osgAnimation::MorphGeometry*>(rig->getSourceGeometry());
			if (rigMorph)
				createMorphTargets(rigMorph, mesh, rotateMatrix);
		}

		return meshNode;
	}

}




