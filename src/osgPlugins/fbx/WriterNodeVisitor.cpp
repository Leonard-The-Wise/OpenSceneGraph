// -*-c++-*-

/*
 * FBX writer for Open Scene Graph
 *
 * Copyright (C) 2009
 *
 * Writing support added 2009 by Thibault Caporal and Sukender (Benoit Neil - http://sukender.free.fr)
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
                         ListTriangle&        listTriangles,
                         unsigned int         drawable_n,
                         unsigned int         material) :
        _drawable_n(drawable_n),
        _listTriangles(listTriangles),
        _modeCache(0),
        _hasNormalCoords(geo->getNormalArray() != NULL),
        _hasTexCoords(geo->getTexCoordArray(0) != NULL),
        _geo(geo),
        _lastFaceIndex(0),
        _material(material),
        _curNormalIndex(0),
        _normalBinding(osg::Geometry::BIND_OFF),
        _mesh(0)
    {
        _normalBinding = geo->getNormalBinding();
        if (!geo->getNormalArray() || geo->getNormalArray()->getNumElements()==0)
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
        if (_normalBinding == osg::Geometry::BIND_PER_VERTEX){
            triangle.normalIndex1 = i1;
            triangle.normalIndex2 = i2;
            triangle.normalIndex3 = i3;
        }
        else{
            triangle.normalIndex1 = _curNormalIndex;
            triangle.normalIndex2 = _curNormalIndex;
            triangle.normalIndex3 = _curNormalIndex;
        }
        triangle.material = _material;
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
        if (indices==0 || count==0) return;

        typedef const T* IndexPointer;

        switch (mode)
        {
        case GL_TRIANGLES:
            {
                IndexPointer ilast = indices + count;
                for (IndexPointer iptr = indices; iptr < ilast; iptr+=3)
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
    ListTriangle&        _listTriangles;
    GLenum               _modeCache;
    std::vector<GLuint>  _indexCache;
    bool                 _hasNormalCoords, _hasTexCoords;
    const osg::Geometry* _geo;
    unsigned int         _lastFaceIndex;
    int                  _material;
    unsigned int         _curNormalIndex;
    osg::Geometry::AttributeBinding _normalBinding;
    FbxMesh*            _mesh;
};

void PrimitiveIndexWriter::drawArrays(GLenum mode,GLint first,GLsizei count)
{
    unsigned int pos=first;
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
            writeTriangle(first, pos, pos+1);
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

WriterNodeVisitor::Material::Material(WriterNodeVisitor& writerNodeVisitor,
                                      osgDB::ExternalFileWriter & externalWriter,
                                      const osg::StateSet* stateset,
                                      const osg::Material* mat,
                                      const osg::Texture* tex,
                                      FbxManager* pSdkManager,
                                      const osgDB::ReaderWriter::Options * options,
                                      int index) :
    _fbxMaterial(NULL),
    _fbxTexture(NULL),
    _index(index),
    _osgImage(NULL)
{
    osg::Vec4 diffuse(1,1,1,1),
              ambient(0.2,0.2,0.2,1),
              specular(0,0,0,1),
              emission(1,1,1,1);

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
    if (tex && tex->getImage(0))
    {
        _osgImage = tex->getImage(0);

        std::string relativePath;
        externalWriter.write(*_osgImage, options, NULL, &relativePath);

        _fbxTexture = FbxFileTexture::Create(pSdkManager, relativePath.c_str());
        _fbxTexture->SetFileName(relativePath.c_str());
        // Create a FBX material if needed
        if (!_fbxMaterial)
        {
            _fbxMaterial = FbxSurfacePhong::Create(pSdkManager, relativePath.c_str());
        }
        // Connect texture to material's diffuse
        // Note there should be no reason FbxSurfacePhong::Create() would return NULL, but as previous code made this secirity test, here we keep the same way.
        if (_fbxMaterial)
        {
            _fbxMaterial->Diffuse.ConnectSrcObject(_fbxTexture);
        }
    }
}

int WriterNodeVisitor::processStateSet(const osg::StateSet* ss)
{
    //OSG_ALWAYS << "Trying Adding " << ss->getAttribute(osg::StateAttribute::MATERIAL)->getName() << std::endl;
    MaterialMap::iterator itr = _materialMap.find(MaterialMap::key_type(ss));
    if (itr != _materialMap.end())
    {
        if (itr->second.getIndex() < 0)
            itr->second.setIndex(_lastMaterialIndex++);
        return itr->second.getIndex();
    }

    const osg::Material* mat = dynamic_cast<const osg::Material*>(ss->getAttribute(osg::StateAttribute::MATERIAL));
    const osg::Texture* tex = dynamic_cast<const osg::Texture*>(ss->getTextureAttribute(0, osg::StateAttribute::TEXTURE));

    if (mat || tex)
    {
        int matNum = _lastMaterialIndex;
        _materialMap.insert(MaterialMap::value_type(MaterialMap::key_type(ss),
            Material(*this, _externalWriter, ss, mat, tex, _pSdkManager, _options, matNum)));
        ++_lastMaterialIndex;
        return matNum;
    }
    return -1;
}

unsigned int addPolygon(MapIndices & index_vert, unsigned int vertIndex, unsigned int normIndex, unsigned int drawableNum)
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

void addPolygon(FbxMesh * mesh, MapIndices & index_vert, const Triangle & tri, unsigned int drawableNum)
{
    mesh->AddPolygon(addPolygon(index_vert, tri.t1, tri.normalIndex1, drawableNum));
    mesh->AddPolygon(addPolygon(index_vert, tri.t2, tri.normalIndex2, drawableNum));
    mesh->AddPolygon(addPolygon(index_vert, tri.t3, tri.normalIndex3, drawableNum));
}


void
WriterNodeVisitor::setLayerTextureAndMaterial(FbxMesh* mesh)
{
    FbxLayerElementTexture* lTextureDiffuseLayer = FbxLayerElementTexture::Create(mesh, "Diffuse");
    lTextureDiffuseLayer->SetMappingMode(FbxLayerElement::eByPolygon);
    lTextureDiffuseLayer->SetReferenceMode(FbxLayerElement::eIndexToDirect);

    FbxLayerElementMaterial* lMaterialLayer = FbxLayerElementMaterial::Create(mesh, "materialLayer");
    lMaterialLayer->SetMappingMode(FbxLayerElement::eByPolygon);
    lMaterialLayer->SetReferenceMode(FbxLayerElement::eIndexToDirect);

    lTextureDiffuseLayer->GetDirectArray().SetCount(_lastMaterialIndex);
    lMaterialLayer->mDirectArray->SetCount(_lastMaterialIndex);

    for (MaterialMap::iterator it = _materialMap.begin(); it != _materialMap.end(); ++it)
    {
        if (it->second.getIndex() != -1)
        {
            FbxSurfaceMaterial* lMaterial = it->second.getFbxMaterial();
            FbxFileTexture* lTexture = it->second.getFbxTexture();
            lTextureDiffuseLayer->GetDirectArray().SetAt(it->second.getIndex(), lTexture);
            lMaterialLayer->mDirectArray->SetAt(it->second.getIndex(), lMaterial);
        }
    }
    mesh->GetLayer(0)->SetMaterials(lMaterialLayer);
    mesh->GetLayer(0)->SetTextures(FbxLayerElement::eTextureDiffuse, lTextureDiffuseLayer);
}

void
WriterNodeVisitor::setControlPointAndNormalsAndUV(const GeometryList& geometryList,
                                                  MapIndices&       index_vert,
                                                  bool              texcoords,
                                                  FbxMesh*         mesh)
{
    mesh->InitControlPoints(index_vert.size());
    FbxLayerElementNormal* lLayerElementNormal= FbxLayerElementNormal::Create(mesh, "");
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

        const osg::Array * basevecs = pGeometry->getVertexArray();
        assert(basevecs);
        if (!basevecs || basevecs->getNumElements()==0)
        {
            continue;
        }
        FbxVector4 vertex;

        switch (basevecs->getType())
        {
        case osg::Array::Vec4ArrayType:
        {
            const osg::Vec4& vec = (*static_cast<const osg::Vec4Array*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z(), vec.w());
            break;
        }
        case osg::Array::Vec4dArrayType:
        {
            const osg::Vec4d& vec = (*static_cast<const osg::Vec4dArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z(), vec.w());
            break;
        }
        case osg::Array::Vec4uiArrayType:
        {
            const osg::Vec4ui& vec = (*static_cast<const osg::Vec4uiArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z(), vec.w());
            break;
        }
        case osg::Array::Vec4usArrayType:
        {
            const osg::Vec4us& vec = (*static_cast<const osg::Vec4usArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z(), vec.w());
            break;
        }
        case osg::Array::Vec4ubArrayType:
        {
            const osg::Vec4ub& vec = (*static_cast<const osg::Vec4ubArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z(), vec.w());
            break;
        }
        case osg::Array::Vec4sArrayType:
        {
            const osg::Vec4s& vec = (*static_cast<const osg::Vec4sArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z(), vec.w());
            break;
        }
        case osg::Array::Vec4bArrayType:
        {
            const osg::Vec4b& vec = (*static_cast<const osg::Vec4bArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z(), vec.w());
            break;
        }

        case osg::Array::Vec3ArrayType:
        {
            const osg::Vec3& vec = (*static_cast<const osg::Vec3Array*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z());
            break;
        }
        case osg::Array::Vec3dArrayType:
        {
            const osg::Vec3d& vec = (*static_cast<const osg::Vec3dArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z());
            break;
        }
        case osg::Array::Vec3uiArrayType:
        {
            const osg::Vec3ui& vec = (*static_cast<const osg::Vec3uiArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z());
            break;
        }
        case osg::Array::Vec3usArrayType:
        {
            const osg::Vec3us& vec = (*static_cast<const osg::Vec3usArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z());
            break;
        }
        case osg::Array::Vec3ubArrayType:
        {
            const osg::Vec3ub& vec = (*static_cast<const osg::Vec3ubArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z());
            break;
        }
        case osg::Array::Vec3iArrayType:
        {
            const osg::Vec3i& vec = (*static_cast<const osg::Vec3iArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z());
            break;
        }
        case osg::Array::Vec3sArrayType:
        {
            const osg::Vec3s& vec = (*static_cast<const osg::Vec3sArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z());
            break;
        }
        case osg::Array::Vec3bArrayType:
        {
            const osg::Vec3b& vec = (*static_cast<const osg::Vec3bArray*>(basevecs))[vertexIndex];
            vertex.Set(vec.x(), vec.y(), vec.z());
            break;
        }
        default:
        {
            OSG_NOTIFY(osg::FATAL) << "Error parsing vertex array." << std::endl;
            throw "FATAL: Vertex array is not Vec3 or Vec3d. Not implemented";
        }
        }

        mesh->SetControlPointAt(vertex, it->second);

        const osg::Array * basenormals = pGeometry->getNormalArray();

        if (basenormals && basenormals->getNumElements()>0)
        {
            FbxVector4 normal;

            switch (basenormals->getType())
            {
            case osg::Array::Vec4ArrayType:
            {
                const osg::Vec4& vec = (*static_cast<const osg::Vec4Array*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4dArrayType:
            {
                const osg::Vec4d& vec = (*static_cast<const osg::Vec4dArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4uiArrayType:
            {
                const osg::Vec4ui& vec = (*static_cast<const osg::Vec4uiArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4usArrayType:
            {
                const osg::Vec4us& vec = (*static_cast<const osg::Vec4usArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4ubArrayType:
            {
                const osg::Vec4ub& vec = (*static_cast<const osg::Vec4ubArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4iArrayType:
            {
                const osg::Vec4i& vec = (*static_cast<const osg::Vec4iArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4sArrayType:
            {
                const osg::Vec4s& vec = (*static_cast<const osg::Vec4sArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4bArrayType:
            {
                const osg::Vec4b& vec = (*static_cast<const osg::Vec4bArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec3ArrayType:
            {
                const osg::Vec3& vec = (*static_cast<const osg::Vec3Array*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3dArrayType:
            {
                const osg::Vec3d& vec = (*static_cast<const osg::Vec3dArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3uiArrayType:
            {
                const osg::Vec3ui& vec = (*static_cast<const osg::Vec3uiArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3usArrayType:
            {
                const osg::Vec3us& vec = (*static_cast<const osg::Vec3usArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3ubArrayType:
            {
                const osg::Vec3ub& vec = (*static_cast<const osg::Vec3ubArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3iArrayType:
            {
                const osg::Vec3i& vec = (*static_cast<const osg::Vec3iArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3sArrayType:
            {
                const osg::Vec3s& vec = (*static_cast<const osg::Vec3sArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3bArrayType:
            {
                const osg::Vec3b& vec = (*static_cast<const osg::Vec3bArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec2ArrayType:
            {
                const osg::Vec2& vec = (*static_cast<const osg::Vec2Array*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2dArrayType:
            {
                const osg::Vec2d& vec = (*static_cast<const osg::Vec2dArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2uiArrayType:
            {
                const osg::Vec2ui& vec = (*static_cast<const osg::Vec2uiArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2usArrayType:
            {
                const osg::Vec2us& vec = (*static_cast<const osg::Vec2usArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2ubArrayType:
            {
                const osg::Vec2ub& vec = (*static_cast<const osg::Vec2ubArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2iArrayType:
            {
                const osg::Vec2i& vec = (*static_cast<const osg::Vec2iArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2sArrayType:
            {
                const osg::Vec2s& vec = (*static_cast<const osg::Vec2sArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2bArrayType:
            {
                const osg::Vec2b& vec = (*static_cast<const osg::Vec2bArray*>(basenormals))[normalIndex];
                normal.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            default:
            {
                OSG_NOTIFY(osg::FATAL) << "Error parsing normal array." << std::endl;
                throw "FATAL: Normal array is not Vec3 or Vec3d. Not implemented";
            }
            }

            //switch (pGeometry->getNormalBinding())
            //{
            //case osg::Geometry::BIND_PER_PRIMITIVE_SET:
            //case osg::Geometry::BIND_PER_VERTEX:
            //    break;
            //}
            lLayerElementNormal->GetDirectArray().SetAt(it->second, normal);
        }

        if (texcoords)
        {
            const osg::Array * basetexcoords = pGeometry->getTexCoordArray(0);
            if (basetexcoords && basetexcoords->getNumElements()>0)
            {
                FbxVector2 texcoord;
                switch (basetexcoords->getType())
                {
                case osg::Array::Vec2ArrayType:
                {
                    const osg::Vec2& vec = (*static_cast<const osg::Vec2Array*>(basetexcoords))[vertexIndex];
                    texcoord.Set(vec.x(), vec.y());
                    break;
                }
                case osg::Array::Vec2dArrayType:
                {
                    const osg::Vec2d& vec = (*static_cast<const osg::Vec2dArray*>(basetexcoords))[vertexIndex];
                    texcoord.Set(vec.x(), vec.y());
                    break;
                }
                case osg::Array::Vec2uiArrayType:
                {
                    const osg::Vec2ui& vec = (*static_cast<const osg::Vec2uiArray*>(basetexcoords))[vertexIndex];
                    texcoord.Set(vec.x(), vec.y());
                    break;
                }
                case osg::Array::Vec2usArrayType:
                {
                    const osg::Vec2us& vec = (*static_cast<const osg::Vec2usArray*>(basetexcoords))[vertexIndex];
                    texcoord.Set(vec.x(), vec.y());
                    break;
                }
                case osg::Array::Vec2ubArrayType:
                {
                    const osg::Vec2ub& vec = (*static_cast<const osg::Vec2ubArray*>(basetexcoords))[vertexIndex];
                    texcoord.Set(vec.x(), vec.y());
                    break;
                }
                case osg::Array::Vec2iArrayType:
                {
                    const osg::Vec2i& vec = (*static_cast<const osg::Vec2iArray*>(basetexcoords))[vertexIndex];
                    texcoord.Set(vec.x(), vec.y());
                    break;
                }
                case osg::Array::Vec2sArrayType:
                {
                    const osg::Vec2s& vec = (*static_cast<const osg::Vec2sArray*>(basetexcoords))[vertexIndex];
                    texcoord.Set(vec.x(), vec.y());
                    break;
                }
                case osg::Array::Vec2bArrayType:
                {
                    const osg::Vec2b& vec = (*static_cast<const osg::Vec2bArray*>(basetexcoords))[vertexIndex];
                    texcoord.Set(vec.x(), vec.y());
                    break;
                }
                default:
                {
                    OSG_NOTIFY(osg::FATAL) << "Error parsing texcoord array." << std::endl;
                    throw "FATAL: Texture coords array is not Vec2 or Vec2d. Not implemented";
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

            switch (tangents->getType())
            {
            case osg::Array::Vec4ArrayType:
            {
                const osg::Vec4& vec = (*static_cast<const osg::Vec4Array*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4dArrayType:
            {
                const osg::Vec4d& vec = (*static_cast<const osg::Vec4dArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4uiArrayType:
            {
                const osg::Vec4ui& vec = (*static_cast<const osg::Vec4uiArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4usArrayType:
            {
                const osg::Vec4us& vec = (*static_cast<const osg::Vec4usArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4ubArrayType:
            {
                const osg::Vec4ub& vec = (*static_cast<const osg::Vec4ubArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4iArrayType:
            {
                const osg::Vec4i& vec = (*static_cast<const osg::Vec4iArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4sArrayType:
            {
                const osg::Vec4s& vec = (*static_cast<const osg::Vec4sArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec4bArrayType:
            {
                const osg::Vec4b& vec = (*static_cast<const osg::Vec4bArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z(), vec.w());
                break;
            }
            case osg::Array::Vec3ArrayType:
            {
                const osg::Vec3& vec = (*static_cast<const osg::Vec3Array*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3dArrayType:
            {
                const osg::Vec3d& vec = (*static_cast<const osg::Vec3dArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3uiArrayType:
            {
                const osg::Vec3ui& vec = (*static_cast<const osg::Vec3uiArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3usArrayType:
            {
                const osg::Vec3us& vec = (*static_cast<const osg::Vec3usArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3ubArrayType:
            {
                const osg::Vec3ub& vec = (*static_cast<const osg::Vec3ubArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3iArrayType:
            {
                const osg::Vec3i& vec = (*static_cast<const osg::Vec3iArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3sArrayType:
            {
                const osg::Vec3s& vec = (*static_cast<const osg::Vec3sArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z());
                break;
            }
            case osg::Array::Vec3bArrayType:
            {
                const osg::Vec3b& vec = (*static_cast<const osg::Vec3bArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), vec.z());
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
            case osg::Array::Vec2uiArrayType:
            {
                const osg::Vec2ui& vec = (*static_cast<const osg::Vec2uiArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2usArrayType:
            {
                const osg::Vec2us& vec = (*static_cast<const osg::Vec2usArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2ubArrayType:
            {
                const osg::Vec2ub& vec = (*static_cast<const osg::Vec2ubArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2iArrayType:
            {
                const osg::Vec2i& vec = (*static_cast<const osg::Vec2iArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2sArrayType:
            {
                const osg::Vec2s& vec = (*static_cast<const osg::Vec2sArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            case osg::Array::Vec2bArrayType:
            {
                const osg::Vec2b& vec = (*static_cast<const osg::Vec2bArray*>(tangents))[vertexIndex];
                tangent.Set(vec.x(), vec.y(), 0.0);
                break;
            }
            default:
            {
                OSG_NOTIFY(osg::FATAL) << "Error parsing tangent array." << std::endl;
                throw "FATAL: Tangent array is not Vec3 or Vec3d. Not implemented";
            }
            }

            lTangentLayer->GetDirectArray().SetAt(it->second, tangent);
        }
    }
}

void WriterNodeVisitor::buildFaces(const std::string& name,
                                   const GeometryList& geometryList,
                                   ListTriangle&     listTriangles,
                                   bool              texcoords)
{
    MapIndices index_vert;
    FbxMesh* mesh = FbxMesh::Create(_pSdkManager, name.c_str());
    _curFbxNode->AddNodeAttribute(mesh);
    _curFbxNode->SetShadingMode(FbxNode::eTextureShading);
    FbxLayer* lLayer = mesh->GetLayer(0);
    if (lLayer == NULL)
    {
        mesh->CreateLayer();
        lLayer = mesh->GetLayer(0);
    }
    setLayerTextureAndMaterial(mesh);
    lLayer->GetTextures(FbxLayerElement::eTextureDiffuse)->GetIndexArray().SetCount(listTriangles.size());
    lLayer->GetMaterials()->GetIndexArray().SetCount(listTriangles.size());
    
    unsigned int i = 0;
    for (ListTriangle::iterator it = listTriangles.begin(); it != listTriangles.end(); ++it, ++i) //Go through the triangle list to define meshs
    {
        if (it->first.material == -1)
        {
            mesh->BeginPolygon();
        }
        else
        {
            mesh->BeginPolygon(i);
            lLayer->GetTextures(FbxLayerElement::eTextureDiffuse)->GetIndexArray().SetAt(i, it->first.material);
            lLayer->GetMaterials()->GetIndexArray().SetAt(i, it->first.material);
        }
        addPolygon(mesh, index_vert, it->first, it->second);
        mesh->EndPolygon();
    }
    setControlPointAndNormalsAndUV(geometryList, index_vert, texcoords, mesh);

	_geometryList.clear();
	_listTriangles.clear();
	_texcoords = false;
	_drawableNum = 0;
	for (MaterialMap::iterator it = _materialMap.begin(); it != _materialMap.end(); ++it)
		it->second.setIndex(-1);
	_lastMaterialIndex = 0;
}


void WriterNodeVisitor::applySkinning(const osgAnimation::VertexInfluenceMap& vim, FbxMesh* fbxMesh)
{
    FbxSkin* skinDeformer = FbxSkin::Create(fbxMesh->GetScene(), "");

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
        FbxCluster* cluster = FbxCluster::Create(fbxMesh->GetScene(), clusterName.str().c_str());
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
        osgInvBindMatrix.postMult(osg::Matrix::rotate(osg::inDegrees(90.0), osg::X_AXIS)); // Fix rotate.

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
                                           ListTriangle&        listTriangles,
                                           bool&                texcoords,
                                           unsigned int         drawable_n)
{
    unsigned int nbVertices = 0;
    {
        const osg::Array * vecs = geo->getVertexArray();
        if (vecs)
        {
            nbVertices = vecs->getNumElements();

            // Texture coords
            const osg::Array * texvec = geo->getTexCoordArray(0);
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

    if (nbVertices==0) return;

    int material = processStateSet(_currentStateSet.get());

    PrimitiveIndexWriter pif(geo, listTriangles, drawable_n, material);
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

    pushStateSet(geometry.getStateSet());
    createListTriangle(&geometry, _listTriangles, _texcoords, _drawableNum++);
    popStateSet(geometry.getStateSet());

    osg::NodeVisitor::traverse(geometry);
    
    if (_listTriangles.size() > 0)
    {
        FbxNode* parent = _curFbxNode;

        FbxNode* nodeFBX = FbxNode::Create(_pSdkManager, geometry.getName().empty() ? "DefaultMesh" : geometry.getName().c_str());
        _curFbxNode->AddChild(nodeFBX);
        _curFbxNode = nodeFBX;

        buildFaces(geometry.getName(), _geometryList, _listTriangles, _texcoords);

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

        if (_listTriangles.size() > 0)
        {
            buildFaces(node.getName(), _geometryList, _listTriangles, _texcoords);
        }

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
        nodeName = node.getName().empty() ? "_rootJoint" : node.getName();
    else if (bone)
        nodeName = node.getName().empty() ? "DefaultBone" : node.getName();
    else
        nodeName = node.getName().empty() ? "DefaultTransform" : node.getName();

    FbxNode* parent = _curFbxNode;
    _curFbxNode = FbxNode::Create(_pSdkManager, skeleton ? "MainSkeleton" : nodeName.c_str());
    parent->AddChild(_curFbxNode);

    if (skeleton || bone)
    {
        FbxSkeleton* fbxSkel = FbxSkeleton::Create(_curFbxNode, nodeName.c_str());
        fbxSkel->SetSkeletonType(skeleton ? FbxSkeleton::eRoot : FbxSkeleton::eLimbNode);
        _curFbxNode->SetNodeAttribute(fbxSkel);

        if (bone)
            _boneNodeMap.emplace(nodeName, std::make_pair(bone, _curFbxNode));
    }

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
