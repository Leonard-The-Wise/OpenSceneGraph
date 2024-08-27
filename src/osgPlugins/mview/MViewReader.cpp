
#include "pch.h"


#include "json.hpp"

#include "MViewReader.h"
#include "MViewFile.h"

using json = nlohmann::json;

using namespace MViewFile;
using namespace MViewParser;

static std::vector<uint8_t> loadFileToVector(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file) 
    {
        OSG_FATAL << "Could not open file " << filename << std::endl;
        throw std::runtime_error("Could not open file " + filename);
    }

    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Create a vector to hold the file data
    std::vector<uint8_t> buffer(fileSize);

    // Read the file into the vector
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) 
    {
        OSG_FATAL << "Error reading file " << filename << std::endl;
        throw std::runtime_error("Error reading file " + filename);
    }

    return buffer;
}


static void writeVectorToFile(const std::string& filename, const std::vector<uint8_t>& data) {
    // Abre o arquivo no modo binário de escrita
    std::ofstream file(filename, std::ios::binary);

    // Verifica se o arquivo foi aberto corretamente
    if (!file) {
        throw std::runtime_error("Could not open file for writting: " + filename);
    }

    // Grava os dados do vetor no arquivo
    file.write(reinterpret_cast<const char*>(data.data()), data.size());

    // Verifica se a gravação foi bem-sucedida
    if (!file) {
        throw std::runtime_error("Error writting file: " + filename);
    }

    // Fecha o arquivo
    file.close();
}

osgDB::ReaderWriter::ReadResult MViewReader::readMViewFile(const std::string& fileName)
{
    OSG_NOTICE << "Loading Marmoset Viewer archive: " << fileName << std::endl;

    _archive = new Archive(loadFileToVector(fileName));

    ArchiveFile sceneFile = _archive->extract("scene.json");

    if (!sceneFile.name.empty())
    {
        if (!_archive->checkSignature(sceneFile))
        {
            OSG_WARN << "WARNING: Invalid MVIEW signature. File may be corrupt." << std::endl;
        }

        std::string fileContents = ByteStream(sceneFile.data).asString();

        OSG_NOTICE << "Parsing MVIEW Scene file" << std::endl;

        json sceneJson;

        try {
            sceneJson = json::parse(fileContents);
        }
        catch (json::parse_error&)
        {
            OSG_FATAL << "Could not parse 'scene.json' in marmoset view archive. File is corrupted" << std::endl;
            return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
        }

        OSG_NOTICE << "Unpacking textures..." << std::endl;

        if (!osgDB::makeDirectory("textures"))
        {
            OSG_FATAL << "Could not create a directory for textures!" << std::endl;
            throw "Exiting...";
        }

        for (auto& textureName : _archive->getTextures())
        {
            OSG_NOTICE << " -> textures/" << textureName << std::endl;
            ArchiveFile textureFile = _archive->extract(textureName);
            if (textureName == "thumbnail.jpg")
                writeVectorToFile(textureName, textureFile.data);
            else
                writeVectorToFile("textures\\" + textureName, textureFile.data);
        }

        return parseScene(sceneJson);
    }
    else
    {
        OSG_FATAL << "Could not read Marmoset view archive " << fileName << ". File might be corrupted" << std::endl;
        return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
    }

	return osgDB::ReaderWriter::ReadResult::FILE_NOT_HANDLED;
}



osg::ref_ptr<osg::Node> MViewReader::parseScene(const json& sceneData)
{
    std::string dump = sceneData.dump();

    fillMetaData(sceneData);

    // Grab meshes
    if (sceneData.contains("meshes") && sceneData["meshes"].is_array())
    {
        for (auto& mesh : sceneData["meshes"])
        {
            ArchiveFile f = _archive->extract(mesh["file"]);
            if (f.name != "")
            {
                Mesh newMesh(mesh, f);

                if (mesh.contains("subMeshes") && mesh["subMeshes"].is_array())
                {
                    if (mesh["subMeshes"].array().size() > 1)
                        OSG_WARN << "WARNING: Current mesh " << newMesh.name << " contains more than 1 submeshes and this is currently unsuported." << std::endl;

                    for (auto& subMesh : mesh["subMeshes"])
                    {
                        if (newMesh.meshMaterial == "")
                            newMesh.meshMaterial = subMesh.value("material", "");
                        newMesh.subMeshes.push_back(SubMesh(subMesh));
                    }
                }
                _meshes.push_back(newMesh);
            }
        }
    }

    // Dummy test: Create a root node, a matrix, a geode and attach meshes to them
    osg::ref_ptr<osg::Group> rootNode = new osg::Group();
    osg::ref_ptr<osg::MatrixTransform> rootMatrix = new osg::MatrixTransform();
    osg::ref_ptr<osg::Geode> rootMesh = new osg::Geode();

    rootMesh->setName("RootNode");
    rootMatrix->setName(_modelName);

    for (auto& mesh : _meshes)
    {
        rootMesh->addDrawable(mesh.asGeometry());
    }

    rootMatrix->addChild(rootMesh);
    rootNode->addChild(rootMatrix);


    return rootNode;
}

void MViewParser::MViewReader::fillMetaData(const json& sceneData)
{
    // Grab metadata
    if (sceneData.contains("metaData"))
    {
        _modelName = sceneData["metaData"].value("title", "Imported MVIEW Scene");
        _modelAuthor = sceneData["metaData"].value("author", "");
        _modelLink = sceneData["metaData"].value("link", "");
        _modelVersion = sceneData["metaData"].value("tbVersion", 0);
    }
}


MViewParser::Mesh::Mesh(const nlohmann::json& description, const MViewFile::ArchiveFile& archiveFile)
{
    desc = description;
    descDump = description.dump();

    isDynamicMesh = desc.value("isDynamicMesh", false);
    cullBackFaces = desc.value("cullBackFaces", false);

    isAnimated = false;

    name = desc.value("name", "");
    file = desc.value("file", "");

    meshMatrix = new osg::MatrixTransform();
    meshMatrix->setName(name);
    if (desc.contains("transform")) {
        const json& t = desc["transform"];
        origin.set(t[12], t[13], t[14]);
        meshMatrix->setMatrix(osg::Matrix(t[0], t[1], t[2], t[3],
                                          t[4], t[5], t[6], t[7],
                                          t[8], t[9], t[10], t[11],
                                          t[12], t[13], t[14], t[15]));
    }
    else {
        origin.set(0, 5, 0);
    }

    /*
    *  (Stride in bytes)
    * Vertex    = 3 * float = 12
      UV        = 2 * float = 8  
      Optional:
          (UV2   = 2 * float = 8)
      Tangent   = 2 * uint_16 = 4  
      BiTangent = 2 * uint_16 = 4  
      Normal    = 2 * uint_16 = 4  

      Stride    = 32 (or 40 with optional)

      Optional final:
        VertexColor = 4 * uint8_t = 4
    */
    stride = 32;
    if (hasVertexColor = desc.value("vertexColor", 0)) 
    {
        stride += 4;
    }
    if (hasSecondaryTexCoord = desc.value("secondaryTexCoord", 0)) {
        stride += 8;
    }

    MViewFile::ByteStream bs(archiveFile.data);
    indexCount = desc.value("indexCount", 0);
    indexTypeSize = desc.value("indexTypeSize", 2);
    std::vector<uint8_t> indexBuffer = bs.readBytes(static_cast<size_t>(indexCount) * indexTypeSize);

    wireCount = desc.value("wireCount", 0);
    std::vector<uint8_t> indexWireBuffer = bs.readBytes(static_cast<size_t>(wireCount) * indexTypeSize);

    vertexCount = desc.value("vertexCount", 0);
    std::vector<uint8_t> vertexData = bs.readBytes(static_cast<size_t>(vertexCount) * stride);

    // Create real arrays
    int b = 0;
    int d = b;
    b = b + 12 + 8;

    int uvStride = 0;
    if (hasSecondaryTexCoord)
    {
        b += 8;
        uvStride = 2;
    }

    int e = b;
    int f = b += 4;
    b = b + 4;

    int g = stride / 2; // Stride in 16bit WORD

    const float* c = reinterpret_cast<const float*>(vertexData.data());

    const uint8_t* ePtr = vertexData.data() + e;
    const uint16_t* tangentsArray = reinterpret_cast<const uint16_t*>(ePtr);

    const uint8_t* fPtr = vertexData.data() + f;
    const uint16_t* bitangentsArray = reinterpret_cast<const uint16_t*>(fPtr);

    // We aren't using tangents, so stop warnings.
    tangentsArray = bitangentsArray = nullptr;

    const uint8_t* bPtr = vertexData.data() + b;
    const uint16_t* normalsArray = reinterpret_cast<const uint16_t*>(bPtr);

    osg::ref_ptr<osg::FloatArray> unTransformedNormals = new osg::FloatArray();
    unTransformedNormals->resizeArray(static_cast<size_t>(3) * vertexCount);
    unpackUnitVectors(unTransformedNormals, normalsArray, vertexCount, g);

    osg::ref_ptr<osg::FloatArray> vertexArray = new osg::FloatArray();
    vertexArray->resizeArray(static_cast<size_t>(3) * vertexCount);
    osg::ref_ptr<osg::FloatArray> uvArray = new osg::FloatArray();
    uvArray->resizeArray(static_cast<size_t>(2) * vertexCount);

    osg::ref_ptr<osg::UByteArray> colorsArray;
    if (hasVertexColor)
    {
        colorsArray = new osg::UByteArray();
        colorsArray->resizeArray(4 * vertexCount);
    }

    osg::ref_ptr<osg::FloatArray> uvArray2;
    if (hasSecondaryTexCoord)
    {
        uvArray2 = new osg::FloatArray();
        uvArray2->resizeArray(static_cast<size_t>(2) * vertexCount);
    }

    for (int i = 0; i < vertexCount; i++)
    {
        int fstride = (stride * i + d) / 4; // Stride in 4 byte Floats
        (*vertexArray)[static_cast<size_t>(3) * i] = c[fstride];
        (*vertexArray)[static_cast<size_t>(3) * i + 1] = c[fstride + 1];
        (*vertexArray)[static_cast<size_t>(3) * i + 2] = c[fstride + 2];

        (*uvArray)[static_cast<size_t>(2) * i] = c[fstride + 3];
        (*uvArray)[static_cast<size_t>(2) * i + 1] = c[fstride + 4];

        if (hasSecondaryTexCoord)
        {
            (*uvArray2)[static_cast<size_t>(2) * i] = c[fstride + 5];
            (*uvArray2)[static_cast<size_t>(2) * i + 1] = c[fstride + 6];
        }

        if (hasVertexColor)
        {
            const uint8_t* colorBytes = reinterpret_cast<const uint8_t*>(c + fstride + 11 + uvStride);
            (*colorsArray)[static_cast<size_t>(4) * i] = colorBytes[0];
            (*colorsArray)[static_cast<size_t>(4) * i + 1] = colorBytes[1];
            (*colorsArray)[static_cast<size_t>(4) * i + 2] = colorBytes[2];
            (*colorsArray)[static_cast<size_t>(4) * i + 3] = colorBytes[3];
        }
    }

    vertex = osg::dynamic_pointer_cast<osg::Vec3Array>(ParserHelper::recastArray(vertexArray, DesiredVectorSize::Vec3));
    normals = osg::dynamic_pointer_cast<osg::Vec3Array>(ParserHelper::recastArray(unTransformedNormals, DesiredVectorSize::Vec3));
    texCoords = osg::dynamic_pointer_cast<osg::Vec2Array>(ParserHelper::recastArray(uvArray, DesiredVectorSize::Vec2));
    texCoords2 = osg::dynamic_pointer_cast<osg::Vec2Array>(ParserHelper::recastArray(uvArray2, DesiredVectorSize::Vec2));
    colors = osg::dynamic_pointer_cast<osg::Vec4ubArray>(ParserHelper::recastArray(colorsArray, DesiredVectorSize::Vec4));

    indices = new osg::DrawElementsUInt();
    indices->setMode(GL_TRIANGLES);

    if (indexTypeSize == 2)
    {
        const uint16_t* indicesUShort = reinterpret_cast<const uint16_t*>(indexBuffer.data());
        int indexSize = indexBuffer.size() / 2;
        indices->reserveElements(indexSize);
        for (int i = 0; i < indexSize; ++i)
        {
            indices->push_back(indicesUShort[i]);
        }
    }
    else
    {
        const uint32_t* indicesUInt = reinterpret_cast<const uint32_t*>(indexBuffer.data());
        int indexSize = indexBuffer.size() / 4;
        indices->reserveElements(indexSize);
        for (int i = 0; i < indexSize; ++i)
        {
            indices->push_back(indicesUInt[i]);
        }
    }


    if (!desc.contains("minBound") || !desc.contains("maxBound")) {
        bounds.min.set(-10, -10, -10);
        bounds.max.set(10, 10, 0);
    }
    else {
        bounds.min.set(desc["minBound"][0], desc["minBound"][1], desc["minBound"][2]);
        bounds.max.set(desc["maxBound"][0], desc["maxBound"][1], desc["maxBound"][2]);
    }
    bounds.maxExtent = std::max({ bounds.max.x() - bounds.min.x(),
                                 bounds.max.y() - bounds.min.y(),
                                 bounds.max.z() - bounds.min.z() });
    bounds.averageExtent = (bounds.max.x() - bounds.min.x() +
        bounds.max.y() - bounds.min.y() +
        bounds.max.z() - bounds.min.z()) / 3;
}

osg::ref_ptr<osg::Geometry> MViewParser::Mesh::asGeometry()
{
    osg::ref_ptr<osgAnimation::RigGeometry> rigGeometry;
    osg::ref_ptr<osg::Geometry> trueGeometry = new osg::Geometry();

    if (isAnimated)
    {
        rigGeometry = new osgAnimation::RigGeometry();
        rigGeometry->setName(name);
    }
    else
        trueGeometry->setName(name);

    // Set arrays
    trueGeometry->setVertexArray(vertex);
    trueGeometry->setNormalArray(normals);

    if (colors)
        trueGeometry->setColorArray(colors);

    trueGeometry->setTexCoordArray(0, texCoords);
    if (texCoords2)
        trueGeometry->setTexCoordArray(1, texCoords2);

    trueGeometry->addPrimitiveSet(indices);

    // Configure rigGeometry
    if (isAnimated)
    {
        rigGeometry->setSourceGeometry(trueGeometry);

        // TODO: Make influenceMap

        // Other data
        rigGeometry->setDataVariance(osg::Object::DYNAMIC);
        rigGeometry->setUseDisplayList(false);

        // TODO: Set materials
    }
    else
    {
        // TODO: Set materials for unrigged mesh
    }


    return isAnimated ? rigGeometry->asGeometry() : trueGeometry;
}

osg::ref_ptr<osg::MatrixTransform> MViewParser::Mesh::asGeometryInMatrix()
{
    osg::ref_ptr<osg::Geode> rootMesh = new osg::Geode();
    rootMesh->addDrawable(asGeometry());

    meshMatrix->addChild(rootMesh);

    return meshMatrix;
}


void MViewParser::Mesh::unpackUnitVectors(osg::ref_ptr<osg::FloatArray>& returnArray, const uint16_t* buffer, int vCount, int byteStride)
{
    for (int e = 0; e < vCount; ++e) 
    {
        // Obtém os valores inteiros da posição apropriada
        uint16_t f = buffer[byteStride * e];      // Coordenada X
        uint16_t g = buffer[byteStride * e + 1];  // Coordenada Y

        // Verifica o sinal da coordenada Z a partir do valor de Y
        bool h = (g >= 32768);
        if (h) {
            g -= 32768;  // Ajusta g se o sinal estava ativo
        }

        // Converte f e g de inteiros para floats normalizados (-1 a 1)
        float fx = static_cast<float>(f) / 32767.4f * 2.0f - 1.0f;
        float fy = static_cast<float>(g) / 32767.4f * 2.0f - 1.0f;

        // Calcula a coordenada Z usando a relação unitária
        float fz = 1.0f - (fx * fx + fy * fy);
        fz = std::sqrt(fz);
        if (std::isnan(fz)) {
            fz = 0.0f;  // Trata NaN (raiz quadrada de valores negativos)
        }

        // Aplica o sinal correto para Z
        if (h) {
            fz = -fz;
        }

        // Armazena o vetor descompactado no vetor de destino
        (*returnArray)[static_cast<size_t>(3) * e] = fx;
        (*returnArray)[static_cast<size_t>(3) * e + 1] = fy;
        (*returnArray)[static_cast<size_t>(3) * e + 2] = fz;
    }
}

MViewParser::SubMesh::SubMesh(const nlohmann::json& description)
{
    materialName = description.value("material", "");
    firstIndex = description.value("firstIndex", 0);
    indexCount = description.value("indexCount", 0);
    firstWireIndex = description.value("firstWireIndex", 0);
    wireIndexCount = description.value("wireIndexCount", 0);
}
