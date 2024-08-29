#include <cmath>

#include "pch.h"

#include "json.hpp"

#include "MViewReader.h"
#include "MViewFile.h"

using json = nlohmann::json;

using namespace MViewFile;
using namespace MViewParser;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float normalizeAngle(float angleInDegrees)
{
    angleInDegrees = fmod(angleInDegrees, 360.0f);
    if (angleInDegrees < 0.0f) {
        angleInDegrees += 360.0f;
    }
    return angleInDegrees;
}

static float degreesToRadians(float degrees)
{
    return normalizeAngle(degrees) * (M_PI / 180.0f);
}

static float radiansToDegrees(float radians) {
    return radians * (180.0f / M_PI);
}

// Fun��o para converter osg::Quat para �ngulos XYZ em graus
static osg::Vec3 convertQuatToEulerDegrees(const osg::Quat& quat) {
    double q0 = quat.w();  // componente w do quaternion
    double q1 = quat.x();  // componente x do quaternion
    double q2 = quat.y();  // componente y do quaternion
    double q3 = quat.z();  // componente z do quaternion

    // Calcular os �ngulos de Euler a partir dos componentes do quaternion
    double rotationX = atan2(2.0 * (q0 * q1 + q2 * q3), 1.0 - 2.0 * (q1 * q1 + q2 * q2));
    double rotationY = asin(2.0 * (q0 * q2 - q3 * q1));
    double rotationZ = atan2(2.0 * (q0 * q3 + q1 * q2), 1.0 - 2.0 * (q2 * q2 + q3 * q3));

    // Converter os �ngulos de radianos para graus
    return osg::Vec3(radiansToDegrees(rotationX),
        radiansToDegrees(rotationY),
        radiansToDegrees(rotationZ));
}

template <typename T>
osg::ref_ptr<T> transformArray(osg::ref_ptr<T>& array, osg::Matrix& transform, bool normalize)
{
    osg::ref_ptr<osg::Array> returnArray;
    osg::Matrix transposeInverse = transform;
    transposeInverse.transpose(transposeInverse);
    transposeInverse = osg::Matrix::inverse(transposeInverse);

    switch (array->getType())
    {
    case osg::Array::Vec4ArrayType:
    {
        returnArray = new osg::Vec4Array();
        returnArray->reserveArray(array->getNumElements());
        for (auto& vec : *osg::dynamic_pointer_cast<const osg::Vec4Array>(array))
        {
            osg::Vec4 v;
            if (normalize)
            {
                osg::Vec3 tangentVec3;
                if (vec.x() == 0.0f && vec.y() == 0.0f && vec.z() == 0.0f) // Fix non-direction vectors (paliative)
                    tangentVec3 = osg::Vec3(1.0f, 0.0f, 0.0f);
                else
                    tangentVec3 = osg::Vec3(vec.x(), vec.y(), vec.z());
                tangentVec3 = tangentVec3 * transposeInverse;
                tangentVec3.normalize();
                v = osg::Vec4(tangentVec3.x(), tangentVec3.y(), tangentVec3.z(), vec.w());
            }
            else
                v = vec * transform;
            osg::dynamic_pointer_cast<osg::Vec4Array>(returnArray)->push_back(v);
        }
        break;
    }
    case osg::Array::Vec3ArrayType:
    {
        returnArray = new osg::Vec3Array();
        returnArray->reserveArray(array->getNumElements());
        for (auto& vec : *osg::dynamic_pointer_cast<const osg::Vec3Array>(array))
        {
            osg::Vec3 v;
            if (normalize)
            {
                v = vec * transposeInverse;
                if (v.x() == 0.0f && v.y() == 0.0f && v.z() == 0.0f)  // Fix non-direction vector (paliative)
                    v = osg::Vec3(1.0f, 0.0f, 0.0f);
                v.normalize();
            }
            else
                v = vec * transform;

            osg::dynamic_pointer_cast<osg::Vec3Array>(returnArray)->push_back(v);
        }
        break;
    }
    default:
        OSG_WARN << "WARNING: Unsuported array to transform." << std::endl;
    }

    return osg::dynamic_pointer_cast<T>(returnArray);
}



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
    // Abre o arquivo no modo bin�rio de escrita
    std::ofstream file(filename, std::ios::binary);

    // Verifica se o arquivo foi aberto corretamente
    if (!file) {
        throw std::runtime_error("Could not open file for writting: " + filename);
    }

    // Grava os dados do vetor no arquivo
    file.write(reinterpret_cast<const char*>(data.data()), data.size());

    // Verifica se a grava��o foi bem-sucedida
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

        //OSG_NOTICE << "Unpacking textures..." << std::endl;

        //if (!osgDB::makeDirectory("textures"))
        //{
        //    OSG_FATAL << "Could not create a directory for textures!" << std::endl;
        //    throw "Exiting...";
        //}

        //for (auto& textureName : _archive->getTextures())
        //{
        //    OSG_NOTICE << " -> textures/" << textureName << std::endl;
        //    ArchiveFile textureFile = _archive->extract(textureName);
        //    if (textureName == "thumbnail.jpg")
        //        writeVectorToFile(textureName, textureFile.data);
        //    else
        //        writeVectorToFile("textures\\" + textureName, textureFile.data);
        //}

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
    std::string sceneJson = sceneData.dump();

    // Fill up metadata
    fillMetaData(sceneData);

    // Get all model meshes
    getMeshes(sceneData);

    // Get skinning information
    bool hasAnimations = parseAnimations(sceneData);

    // Dummy test: Create a root node, a matrix, a geode and attach meshes to them
    osg::ref_ptr<osg::Group> rootNode = new osg::Group();
    osg::ref_ptr<osg::MatrixTransform> rootMatrix = new osg::MatrixTransform();
    osg::ref_ptr<osg::Geode> rootMesh = new osg::Geode();

    rootMesh->setName("RootNode");
    rootMatrix->setName(_modelName);

    if (hasAnimations)
    {
        osg::ref_ptr<osgAnimation::Skeleton> meshSkeleton = buildBones();

        for (auto& mesh : _meshes)
        {
            if (mesh.isAnimated)
                meshSkeleton->addChild(mesh.asGeometryInMatrix());
            else
                rootMesh->addDrawable(mesh.asGeometry());
        }

        rootMatrix->addChild(meshSkeleton);
        //rootMatrix->setMatrix(osg::Matrixd::rotate(osg::Z_AXIS, -osg::Y_AXIS));

        //osg::ref_ptr<osgAnimation::BasicAnimationManager> bam = buildAnimationManager();
        //rootNode->addUpdateCallback(bam);
    }
    else
    {
        for (auto& mesh : _meshes)
        {
            rootMesh->addDrawable(mesh.asGeometry());
        }
    }

    rootMatrix->addChild(rootMesh);
    //rootMatrix->setUserValue("firstMatrix", true);

    rootNode->addChild(rootMatrix);

    rootNode->setUserValue("MVIEWScene", sceneJson);

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

void MViewParser::MViewReader::getMeshes(const json& sceneData)
{
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
}

bool MViewParser::MViewReader::parseAnimations(const json& sceneData)
{
    if (!sceneData.contains("AnimData"))
        return false;

    const json& animData = sceneData["AnimData"];

    if (animData.contains("meshIDs"))
    {
        for (auto& meshID : animData["meshIDs"])
        {
            _meshIDs.push_back(meshID.value("partIndex", -1));
        }
    }

    if (animData.contains("materialIDs"))
    {
        for (auto& materialID : animData["materialIDs"])
        {
            _materialIDs.push_back(materialID.value("partIndex", -1));
        }
    }

    _numMatricesInTable = animData.value("numMatrices", 0);

    if (animData.contains("skinningRigs") && _numMatricesInTable > 0)
    {
        ArchiveFile e = _archive->get("MatTable.bin");
        ByteStream f(e.data);

        for (auto& skinningRig : animData["skinningRigs"])
        {
            SkinningRig newSkin(*_archive, skinningRig, f);
            if (newSkin.isRigValid)
                _skinningRigs.push_back(newSkin);
        }
    }

    if (animData.contains("animations"))
    {
        for (auto& animation : animData["animations"])
        {
            _animations.push_back(Animation(*_archive, animation));
        }
    }


    return true;
}

int MViewParser::MViewReader::getMeshIndexFromID(int id)
{
    int i = 0;
    for (int meshID : _meshIDs)
    {
        if (meshID == id)
            return i;
        ++i;
    }

    return -1;
}

int MViewParser::MViewReader::getSkinningRigIDForlinkObject(int linkID)
{
    for (int i = 0; i < _skinningRigs.size(); ++i)
    {
        for (auto& skinningCluster : _skinningRigs[i].skinningClusters)
        {
            if (linkID == skinningCluster.linkObjectIndex)
                return i;
        }
    }

    return -1;
}

AnimatedObject* MViewParser::MViewReader::getAnimatedObject(std::vector<AnimatedObject>& animatedObjects, int id)
{
    for (auto& animatedObject : animatedObjects)
    {
        if (animatedObject.id == id)
            return &animatedObject;
    }

    return nullptr;
}

osg::Matrix MViewParser::MViewReader::createBoneTransform(AnimatedObject& modelPart, AnimatedObject& linkObject,
    int linkMode, const osg::Matrix& defaultClusterBaseTransform, const osg::Matrix& defaultClusterWorldTransform)
{
    osg::Matrix linkTransform = linkObject.getWorldTransform();
    osg::Matrix partTransform = modelPart.getWorldTransform();
    osg::Matrix invertedPartTransform = osg::Matrix::inverse(partTransform);

    osg::Matrix intermediateMatrix = linkTransform * invertedPartTransform;

    // 3. Multiplica��o pela transforma��o base do cluster
    osg::Matrix boneTransform = intermediateMatrix * defaultClusterBaseTransform;

    return boneTransform;
}

osg::Matrix MViewParser::MViewReader::extractBoneTransform(const osg::Matrix& outputMatrix, const osg::Matrix& defaultClusterBaseTransform) 
{
    osg::Matrix inverseBindMatrix = osg::Matrix::inverse(defaultClusterBaseTransform);

    osg::Matrix boneTransform = outputMatrix * inverseBindMatrix;

    return boneTransform;
}


osg::ref_ptr<osgAnimation::Skeleton> MViewParser::MViewReader::buildBones()
{
    osg::ref_ptr<osgAnimation::Skeleton> returnSkeleton = new osgAnimation::Skeleton();
    returnSkeleton->setDataVariance(osg::Object::DYNAMIC);
    returnSkeleton->setName("Armature");

    osg::ref_ptr<osgAnimation::Bone> rootBone = new osgAnimation::Bone();
    rootBone->setName("RootBone");

    returnSkeleton->addChild(rootBone);

    // Get all bone part from skinning clusters along with cluster information
    std::map<int, SkinningCluster> modelBonePartIDs;

    for (auto& skin : _skinningRigs)
    {
        for (auto& cluster : skin.skinningClusters)
        {
            if (modelBonePartIDs.find(cluster.linkObjectIndex) == modelBonePartIDs.end())
                modelBonePartIDs[cluster.linkObjectIndex] = cluster;
            else
            {
                SkinningCluster otherCluster = modelBonePartIDs[cluster.linkObjectIndex];
                OSG_DEBUG << "Conflict between clusters." << std::endl;
            }
        }
    }

    // Search animation nodes for given bone part number
    for (auto& animation : _animations)
    {
        for (auto& animationObj : animation.animatedObjects)
        {
            if (animationObj.sceneObjectType == "Node" && animationObj.skinningRigIndex == -1 && animationObj.parentIndex == 0)
            {
                _modelBonePartNames[animationObj.modelPartIndex] = animationObj.partName;
            }

            if (animationObj.sceneObjectType == "MeshSO" /* && animationObj.skinningRigIndex > -1*/)
            {
                int realMeshID = getMeshIndexFromID(animationObj.id);
                _skinIDToMeshID[animationObj.skinningRigIndex] = realMeshID;
                _meshIDtoSkinID[realMeshID] = animationObj.skinningRigIndex;
                _meshes[realMeshID].meshSOReference = animationObj.id;

                auto& nodeTransform = animation.animatedObjects[animationObj.modelPartIndex];
                _meshes[realMeshID].setAnimatedTransform(nodeTransform);
            }
        }
        break;
    }

    // For each bone found, try to get the best model part and link on animations that can give us matrices to calculate the bone space
    for (auto& bonePartName : _modelBonePartNames)
    {
        int linkObjectID = bonePartName.first;
        std::string boneName = bonePartName.second;

        bool found = false;
        for (auto& animation : _animations)
        {
            for (auto& animationObj : animation.animatedObjects)
            {
                if (animationObj.modelPartIndex == linkObjectID)
                {
                    // Find part ID on skinning cluster, so we can trace which mesh to get
                    int skinningRigID = getSkinningRigIDForlinkObject(linkObjectID);
                    int meshID = _skinIDToMeshID[skinningRigID];
                    AnimatedObject* linkObjectPart = &animationObj;
                    AnimatedObject* meshSOParent = getAnimatedObject(animation.animatedObjects, _meshIDs[meshID]);
                    int modelPartIndex = meshSOParent->modelPartIndex;
                    AnimatedObject* modelPart = getAnimatedObject(animation.animatedObjects, modelPartIndex);

                    _bonesToModelPartAndLinkObject[boneName] = std::make_pair(modelPart, linkObjectPart);

                    found = true;
                    break;
                }
            }

            if (found)
                break;
        }
    }


    // Create all bones
    for (auto& modelBone : modelBonePartIDs)
    {
        int id = modelBone.first;
        std::string name = _modelBonePartNames[id];

        osg::ref_ptr<osgAnimation::Bone> newBone = new osgAnimation::Bone();
        newBone->setName(_modelBonePartNames[id]);

        AnimatedObject& modelPart = *_bonesToModelPartAndLinkObject[name].first;
        AnimatedObject& linkObject = *_bonesToModelPartAndLinkObject[name].second;

        osg::Matrix outputMatrix = createBoneTransform(modelPart, linkObject, 
            modelBone.second.linkMode, modelBone.second.defaultClusterBaseTransform,
            modelBone.second.defaultClusterWorldTransform);        

        osg::Matrix boneTransform = extractBoneTransform(outputMatrix, modelBone.second.defaultClusterBaseTransform);
        osg::Matrix invBindMatrix = modelBone.second.defaultClusterBaseTransform;

        newBone->setMatrix(boneTransform);
        newBone->setInvBindMatrixInSkeletonSpace(invBindMatrix);

        osg::ref_ptr<osgAnimation::UpdateBone> updateBone = new osgAnimation::UpdateBone();
        updateBone->setName(_modelBonePartNames[id]);
        newBone->addUpdateCallback(updateBone);

        rootBone->addChild(newBone);
    }

    // Create vertexinfluencemap for each mesh
    for (int i = 0; i < _meshes.size(); ++i)
    {
        if (_skinIDToMeshID.find(i) != _skinIDToMeshID.end())
            _meshes[i].createInfluenceMap(_skinningRigs[_skinIDToMeshID[i]], _modelBonePartNames);
    }

    return returnSkeleton;
}

osg::ref_ptr<osgAnimation::BasicAnimationManager> MViewParser::MViewReader::buildAnimationManager()
{
    osg::ref_ptr<osgAnimation::BasicAnimationManager> bam = new osgAnimation::BasicAnimationManager();

    for (auto& animation : _animations)
    {
        bam->getAnimationList().push_back(animation.asAnimation());
    }

    return bam;
}


MViewParser::SkinningRig::SkinningRig(Archive& archive, const json& json, ByteStream& byteStream)
{
    isRigValid = false;
    srcVFile = json["file"].get<std::string>();

    // Verifica se o arquivo existe no Archive
    auto archiveFile = archive.get(srcVFile);
    if (!archiveFile.data.empty()) {
        const std::vector<uint8_t>& data = archiveFile.data;

        // Cria um array de Uint32 a partir dos dados do arquivo
        const uint32_t* a = reinterpret_cast<const uint32_t*>(data.data());
        size_t length = data.size() / sizeof(uint32_t);

        if (length >= 6) {
            expectedNumClusters = a[0];
            expectedNumVertices = a[1];
            numClusterLinks = a[2];
            originalObjectIndex = a[3];
            isRigidSkin = static_cast<bool>(a[4]);
            tangentMethod = a[5];

            size_t c = 6 + 7 * expectedNumClusters;

            // Itera sobre os clusters de skinning
            for (int d = 0; d < expectedNumClusters; ++d) {
                SkinningCluster e;

                int f = 6 + 7 * d;
                e.linkMode = a[f + 1];
                e.linkObjectIndex = a[f + 2];
                e.associateObjectIndex = a[f + 3];

                int g = a[f + 5];
                e.defaultClusterWorldTransform = byteStream.getMatrix(a[f + 4]);
                e.defaultClusterBaseTransform = byteStream.getMatrix(g);

                // Invers�o da matriz sem modificar a original
                e.defaultClusterWorldTransformInvert = osg::Matrix::inverse(e.defaultClusterWorldTransform);

                if (e.linkMode == 1) {
                    e.defaultAssociateWorldTransform = byteStream.getMatrix(a[f + 6]);

                    // Invers�o da matriz sem modificar a original
                    e.defaultAssociateWorldTransformInvert = osg::Matrix::inverse(e.defaultAssociateWorldTransform);
                }

                // Adiciona o skinningCluster ao vetor ap�s todas as opera��es
                skinningClusters.push_back(e);
            }

            size_t bIndex = 4 * c;
            size_t cIndex = bIndex + expectedNumVertices;
            size_t aIndex = cIndex + 2 * numClusterLinks;

            // Inicializa o vetor linkMapCount
            linkMapCount.resize(expectedNumVertices);
            std::copy(data.begin() + bIndex, data.begin() + cIndex, linkMapCount.begin());

            // Calcula o tamanho de linkMapClusterIndices com base nos dados
            size_t linkMapClusterIndicesSize = numClusterLinks;//(data.size() - cIndex) / sizeof(uint16_t);
            linkMapClusterIndices.resize(linkMapClusterIndicesSize);
            std::copy(reinterpret_cast<const uint16_t*>(data.data() + cIndex),
                reinterpret_cast<const uint16_t*>(data.data() + cIndex + 2 * linkMapClusterIndicesSize),
                linkMapClusterIndices.begin());

            // Inicializa o vetor linkMapWeights
            linkMapWeights.resize(numClusterLinks);
            std::copy(reinterpret_cast<const float*>(data.data() + aIndex),
                reinterpret_cast<const float*>(data.data() + aIndex + 4 * numClusterLinks),
                linkMapWeights.begin());
        }
        else {
            return;
        }
    }
    else {
        return;
    }

    isRigValid = true;

}

MViewParser::SubMesh::SubMesh(const nlohmann::json& description)
{
    materialName = description.value("material", "");
    firstIndex = description.value("firstIndex", 0);
    indexCount = description.value("indexCount", 0);
    firstWireIndex = description.value("firstWireIndex", 0);
    wireIndexCount = description.value("wireIndexCount", 0);
}

MViewParser::Mesh::Mesh(const nlohmann::json& description, const MViewFile::ArchiveFile& archiveFile)
{
    desc = description;
    descDump = description.dump();

    isDynamicMesh = desc.value("isDynamicMesh", false);
    cullBackFaces = desc.value("cullBackFaces", false);

    isAnimated = false;
    meshSOReference = -1;

    name = desc.value("name", "");
    file = desc.value("file", "");

    meshMatrix = new osg::MatrixTransform();
    meshMatrix->setName(name);

    meshMatrixRigTransform = new osg::MatrixTransform();;
    //if (desc.contains("transform")) {
    //    const json& t = desc["transform"];
    //    origin.set(t[12], t[13], t[14]);
    //    meshMatrix->setMatrix(osg::Matrix(t[0], t[1], t[2], t[3],
    //                                      t[4], t[5], t[6], t[7],
    //                                      t[8], t[9], t[10], t[11],
    //                                      t[12], t[13], t[14], t[15]));
    //}
    //else {
    //    origin.set(0, 5, 0);
    //}

    /*
    *  (Stride in bytes and floats)
    * Vertex    = 3 * float = 12 (0, 1, 2)
      UV        = 2 * float = 8  (3, 4)
      Optional:
          (UV2   = 2 * float = 8)
      Tangent   = 2 * uint_16 = 4  (5)
      BiTangent = 2 * uint_16 = 4  (6)
      Normal    = 2 * uint_16 = 4  (7)

      Stride    = 32 (or 40 with optional)

      Optional final:
        VertexColor = 4 * uint8_t = 4 (8)
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
            const uint8_t* colorBytes = reinterpret_cast<const uint8_t*>(c + fstride + 8 + uvStride);
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

const osg::ref_ptr<osg::Geometry> MViewParser::Mesh::asGeometry()
{
    osg::ref_ptr<osgAnimation::RigGeometry> rigGeometry;
    osg::ref_ptr<osg::Geometry> trueGeometry = new osg::Geometry();

    trueGeometry->setName(name);
    trueGeometry->setUserValue("ModelType", std::string("mview"));

    if (isAnimated)
    {
        rigGeometry = new osgAnimation::RigGeometry();
        rigGeometry->setName(name);
    }

    // Set arrays
    trueGeometry->setVertexArray(vertex);
    trueGeometry->setNormalArray(normals);

    if (colors)
        trueGeometry->setColorArray(colors);

    trueGeometry->setTexCoordArray(0, texCoords);
    if (texCoords2)
        trueGeometry->setTexCoordArray(1, texCoords2);

    trueGeometry->addPrimitiveSet(indices);

    if (subMeshes.size() > 0)
    {
        osg::ref_ptr<osg::StateSet> ss = new osg::StateSet();
        osg::ref_ptr<osg::Material> mat = new osg::Material();

        mat->setName(subMeshes[0].materialName);
        ss->setAttribute(mat, osg::StateAttribute::MATERIAL);

        trueGeometry->setStateSet(ss);
    }

    // Configure rigGeometry
    if (isAnimated)
    {
        rigGeometry->setSourceGeometry(trueGeometry);

        // TODO: Make influenceMap
        rigGeometry->setInfluenceMap(influenceMap);

        // Other data
        rigGeometry->setDataVariance(osg::Object::DYNAMIC);
        rigGeometry->setUseDisplayList(false);
    }


    return isAnimated ? rigGeometry->asGeometry() : trueGeometry;
}

const osg::ref_ptr<osg::MatrixTransform> MViewParser::Mesh::asGeometryInMatrix()
{
    osg::ref_ptr<osg::Geode> rootMesh = new osg::Geode();
    rootMesh->addDrawable(asGeometry());

    meshMatrix->addChild(rootMesh);

    return meshMatrix;
}

void MViewParser::Mesh::setAnimatedTransform(AnimatedObject& referenceNode)
{
    osg::Matrix matrixT = referenceNode.getWorldTransform();
    osg::Matrix animatedMatrixTransform;

    animatedMatrixTransform = matrixT; //* osg::Matrix::rotate(osg::DegreesToRadians(-90.0), osg::X_AXIS);

    osg::ref_ptr<osgAnimation::UpdateMatrixTransform> updateMatrix = new osgAnimation::UpdateMatrixTransform();
    updateMatrix->setName(name);

    osg::ref_ptr<osgAnimation::StackedMatrixElement> sme = new osgAnimation::StackedMatrixElement();
    sme->setMatrix(animatedMatrixTransform);
    updateMatrix->getStackedTransforms().push_back(sme);

    meshMatrix->addUpdateCallback(updateMatrix);
}

void MViewParser::Mesh::createInfluenceMap(const SkinningRig& skinningRig, const std::map<int, std::string>& modelBonePartNames)
{
    influenceMap = new osgAnimation::VertexInfluenceMap();

    int linkMapIndex = 0;

    for (int vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) 
    {
        int linkCount = skinningRig.linkMapCount[vertexIndex];  
        
        for (int weightIndex = 0; weightIndex < linkCount; ++weightIndex) 
        {
            float weight = skinningRig.linkMapWeights[linkMapIndex + weightIndex];
            int clusterIndex = skinningRig.linkMapClusterIndices[linkMapIndex + weightIndex];

            int partNumber = skinningRig.skinningClusters[clusterIndex].linkObjectIndex;

            auto it = modelBonePartNames.find(partNumber);
            if (it != modelBonePartNames.end()) 
            {
                const std::string& boneName = it->second;
                (*influenceMap)[boneName].push_back(std::make_pair(vertexIndex, weight));
            }
        }

        linkMapIndex += linkCount;
    }

    if (influenceMap->size() > 0)
        isAnimated = true;
}

void MViewParser::Mesh::unpackUnitVectors(osg::ref_ptr<osg::FloatArray>& returnArray, const uint16_t* buffer, int vCount, int byteStride)
{
    for (int e = 0; e < vCount; ++e) 
    {
        // Obt�m os valores inteiros da posi��o apropriada
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

        // Calcula a coordenada Z usando a rela��o unit�ria
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

MViewParser::AnimatedObject::AnimatedObject(const Archive& archive, const json& description, int ID) :
    keyFramesByteStream(nullptr)
{
    id = ID;
    partName = description.value("partName", "");
    sceneObjectType = description.value("sceneObjectType", "");
    skinningRigIndex = description.value("skinningRigIndex", -2);
    modelPartIndex = description.value("modelPartIndex", 0);
    parentIndex = description.value("parentIndex", 0);
    modelPartFPS = description.value("modelPartFPS", 0);

    if (description.contains("animatedProperties"))
    {
        for (auto& animatedProperty : description["animatedProperties"])
        {
            AnimatedProperty a;
            a.name = animatedProperty.value("name", "NONE");
            animatedProperties.push_back(a);
        }
    }

    std::string fileName = description.value("file", "");
    ArchiveFile file = archive.get(fileName);
    keyFramesByteStream = new ByteStream(file.data);

    if (file.data.size() > 0)
    {
        unPackKeyFrames();

        for (auto& a : animatedProperties)
            animatedPropertiesMap[a.name] = &a;

        assembleKeyFrames();
    }
}

const osg::Matrix MViewParser::AnimatedObject::getWorldTransform()
{
    osg::Matrix worldTransform;

    if (translation && scale && rotation) 
    {
        osg::Vec3f position = translation->getOrCreateSampler()->getOrCreateKeyframeContainer()->at(0).getValue();
        osg::Vec3f scaling = scale->getOrCreateSampler()->getOrCreateKeyframeContainer()->at(0).getValue();
        osg::Quat orientation = rotation->getOrCreateSampler()->getOrCreateKeyframeContainer()->at(0).getValue();

        osg::Matrix translationMatrix = osg::Matrix::translate(position);
        osg::Matrix rotationMatrix = osg::Matrix::rotate(orientation);
        osg::Matrix scaleMatrix = osg::Matrix::scale(scaling);

        worldTransform = scaleMatrix * rotationMatrix * translationMatrix;
    }

    return worldTransform;
}

void MViewParser::AnimatedObject::unPackKeyFrames() 
{
    if (keyFramesByteStream && !keyFramesByteStream->empty())
    {
        // Inicializa os buffers de keyframes a partir do ByteStream
        keyframesSharedBufferFloats.assign(reinterpret_cast<const float*>(keyFramesByteStream->bytes.data()),
            reinterpret_cast<const float*>(keyFramesByteStream->bytes.data() + keyFramesByteStream->bytes.size()));

        keyframesSharedBufferUShorts.assign(reinterpret_cast<const uint32_t*>(keyFramesByteStream->bytes.data()),
            reinterpret_cast<const uint32_t*>(keyFramesByteStream->bytes.data() + keyFramesByteStream->bytes.size()));

        keyframesSharedBufferShorts.assign(reinterpret_cast<const uint16_t*>(keyFramesByteStream->bytes.data()),
            reinterpret_cast<const uint16_t*>(keyFramesByteStream->bytes.data() + keyFramesByteStream->bytes.size()));

        keyframesSharedBufferBytes.assign(keyFramesByteStream->bytes.begin(), keyFramesByteStream->bytes.end());

        size_t a = 0;
        size_t c = keyframesSharedBufferUShorts[0];
        a = 1 + c;
        
        for (size_t e = 0; e < animatedProperties.size(); ++e)
        {
            auto& f = animatedProperties[e];
            size_t g = 2 + 2 * e;
            size_t h = 2 * g;

            f.keyframeBufferStartIndexFloat = a;
            f.numKeyframes = keyframesSharedBufferShorts[g];
            f.keyframePackingType = keyframesSharedBufferBytes[h + 2];
            f.interpolationType = keyframesSharedBufferBytes[h + 3];
            f.indexFloatSkip = 0;
            f.indexUShortSkip = 0;

            if (f.numKeyframes > 0) {
                switch (f.keyframePackingType) {
                case 0:  // Empacotamento completo
                    f.bytesPerKeyFrame = 16;
                    f.indexFloatSkip = 4;
                    f.indexUShortSkip = 8;
                    f.valueOffsetFloat = 0;
                    f.weighInOffsetFloat = 1;
                    f.weighOutOffsetFloat = 2;
                    f.frameIndexOffsetUShort = 6;
                    f.interpolationOffsetUShort = 7;
                    break;
                case 1:  // Empacotamento reduzido
                    f.bytesPerKeyFrame = 8;
                    f.indexFloatSkip = 2;
                    f.indexUShortSkip = 4;
                    f.valueOffsetFloat = 0;
                    f.weighInOffsetFloat = 0;
                    f.weighOutOffsetFloat = 0;
                    f.frameIndexOffsetUShort = 2;
                    f.interpolationOffsetUShort = 3;
                    break;
                case 2:  // Empacotamento simples
                    f.bytesPerKeyFrame = 4;
                    f.indexFloatSkip = 1;
                    f.indexUShortSkip = 2;
                    f.valueOffsetFloat = 0;
                    f.weighInOffsetFloat = 0;
                    f.weighOutOffsetFloat = 0;
                    f.frameIndexOffsetUShort = 0;
                    f.interpolationOffsetUShort = 0;
                    break;
                }
                a += f.numKeyframes * f.indexFloatSkip;
            }
        }
    }
}

std::vector<std::pair<int, float>> AnimatedObject::extractKeyframes(const AnimatedProperty& property) 
{
    std::vector<std::pair<int, float>> keyframes;

    size_t d = property.keyframeBufferStartIndexFloat;
    size_t numKeyframes = property.numKeyframes;

    if (property.keyframePackingType == 0) {
        // Empacotamento Completo (16 bytes por keyframe)
        for (size_t i = 0; i < numKeyframes; ++i) {
            int frameIndex = keyframesSharedBufferUShorts[d + i * property.indexUShortSkip + property.frameIndexOffsetUShort];
            float value = keyframesSharedBufferFloats[d + i * property.indexFloatSkip + property.valueOffsetFloat];
            keyframes.emplace_back(frameIndex, value);
        }
    }
    else if (property.keyframePackingType == 1) {
        // Empacotamento Reduzido (8 bytes por keyframe)
        for (size_t i = 0; i < numKeyframes; ++i) {
            int frameIndex = keyframesSharedBufferUShorts[d + i * property.indexUShortSkip + property.frameIndexOffsetUShort];
            float value = keyframesSharedBufferFloats[d + i * property.indexFloatSkip + property.valueOffsetFloat];
            keyframes.emplace_back(frameIndex, value);
        }
    }
    else if (property.keyframePackingType == 2) {
        // Empacotamento Simples (4 bytes por keyframe)
        for (size_t i = 0; i < numKeyframes; ++i) {
            int frameIndex = static_cast<int>(i);  // O �ndice do quadro � impl�cito e corresponde ao �ndice do keyframe
            float value = keyframesSharedBufferFloats[d + i * property.indexFloatSkip + property.valueOffsetFloat];
            keyframes.emplace_back(frameIndex, value);
        }
    }

    return keyframes;
}

void MViewParser::AnimatedObject::assembleKeyFrames()
{
    // Get Translation
    auto itrTransX = animatedPropertiesMap.find("Translation X");
    auto itrTransY = animatedPropertiesMap.find("Translation Y");
    auto itrTransZ = animatedPropertiesMap.find("Translation Z");

    if (itrTransX != animatedPropertiesMap.end() || itrTransY != animatedPropertiesMap.end() || itrTransZ != animatedPropertiesMap.end())
    {
        int numKeyFramesX = itrTransX != animatedPropertiesMap.end() ? itrTransX->second->numKeyframes : 0;
        int numKeyFramesY = itrTransY != animatedPropertiesMap.end() ? itrTransY->second->numKeyframes : 0;
        int numKeyFramesZ = itrTransZ != animatedPropertiesMap.end() ? itrTransZ->second->numKeyframes : 0;

        int numKeyFrames = std::max({ numKeyFramesX, numKeyFramesY, numKeyFramesZ });

        osg::ref_ptr<osg::FloatArray> timesArray = new osg::FloatArray(numKeyFrames);

        osg::ref_ptr<osg::FloatArray> transX = new osg::FloatArray(numKeyFrames);
        osg::ref_ptr<osg::FloatArray> transY = new osg::FloatArray(numKeyFrames);
        osg::ref_ptr<osg::FloatArray> transZ = new osg::FloatArray(numKeyFrames);

        // Copy values from extracted keys
        if (itrTransX != animatedPropertiesMap.end())
            copyFromExtractedKeys(itrTransX->first, timesArray, transX);
        if (itrTransY != animatedPropertiesMap.end())
            copyFromExtractedKeys(itrTransY->first, timesArray, transY);
        if (itrTransZ != animatedPropertiesMap.end())
            copyFromExtractedKeys(itrTransZ->first, timesArray, transZ);

        translation = makeVec3LinearFromArrays("translate", timesArray, transX, transY, transZ);
    }

    // Get Scale
    auto itrScaleX = animatedPropertiesMap.find("Scale X");
    auto itrScaleY = animatedPropertiesMap.find("Scale Y");
    auto itrScaleZ = animatedPropertiesMap.find("Scale Z");

    if (itrScaleX != animatedPropertiesMap.end() || itrScaleY != animatedPropertiesMap.end() || itrScaleZ != animatedPropertiesMap.end())
    {
        int numKeyFramesX = itrScaleX != animatedPropertiesMap.end() ? itrScaleX->second->numKeyframes : 0;
        int numKeyFramesY = itrScaleY != animatedPropertiesMap.end() ? itrScaleY->second->numKeyframes : 0;
        int numKeyFramesZ = itrScaleZ != animatedPropertiesMap.end() ? itrScaleZ->second->numKeyframes : 0;

        int numKeyFrames = std::max({ numKeyFramesX, numKeyFramesY, numKeyFramesZ });

        osg::ref_ptr<osg::FloatArray> timesArray = new osg::FloatArray(numKeyFrames);

        osg::ref_ptr<osg::FloatArray> scaleX = new osg::FloatArray(numKeyFrames);
        osg::ref_ptr<osg::FloatArray> scaleY = new osg::FloatArray(numKeyFrames);
        osg::ref_ptr<osg::FloatArray> scaleZ = new osg::FloatArray(numKeyFrames);

        // Copy values from extracted keys
        if (itrScaleX != animatedPropertiesMap.end())
            copyFromExtractedKeys(itrScaleX->first, timesArray, scaleX);
        if (itrScaleY != animatedPropertiesMap.end())
            copyFromExtractedKeys(itrScaleY->first, timesArray, scaleY);
        if (itrScaleZ != animatedPropertiesMap.end())
            copyFromExtractedKeys(itrScaleZ->first, timesArray, scaleZ);

        scale = makeVec3LinearFromArrays("scale", timesArray, scaleX, scaleY, scaleZ);
    }

    // Get Rotation
    auto itrRotationX = animatedPropertiesMap.find("Rotation X");
    auto itrRotationY = animatedPropertiesMap.find("Rotation Y");
    auto itrRotationZ = animatedPropertiesMap.find("Rotation Z");

    if (itrRotationX != animatedPropertiesMap.end() || itrRotationY != animatedPropertiesMap.end() || itrRotationZ != animatedPropertiesMap.end())
    {
        int numKeyFramesX = itrRotationX != animatedPropertiesMap.end() ? itrRotationX->second->numKeyframes : 0;
        int numKeyFramesY = itrRotationY != animatedPropertiesMap.end() ? itrRotationY->second->numKeyframes : 0;
        int numKeyFramesZ = itrRotationZ != animatedPropertiesMap.end() ? itrRotationZ->second->numKeyframes : 0;

        int numKeyFrames = std::max({ numKeyFramesX, numKeyFramesY, numKeyFramesZ });

        osg::ref_ptr<osg::FloatArray> timesArray = new osg::FloatArray(numKeyFrames);

        osg::ref_ptr<osg::FloatArray> rotationX = new osg::FloatArray(numKeyFrames);
        osg::ref_ptr<osg::FloatArray> rotationY = new osg::FloatArray(numKeyFrames);
        osg::ref_ptr<osg::FloatArray> rotationZ = new osg::FloatArray(numKeyFrames);

        // Copy values from extracted keys
        if (itrRotationX != animatedPropertiesMap.end())
            copyFromExtractedKeys(itrRotationX->first, timesArray, rotationX);
        if (itrRotationY != animatedPropertiesMap.end())
            copyFromExtractedKeys(itrRotationY->first, timesArray, rotationY);
        if (itrRotationZ != animatedPropertiesMap.end())
            copyFromExtractedKeys(itrRotationZ->first, timesArray, rotationZ);

        rotation = makeQuatLinearFromArrays("quaternion", timesArray, rotationX, rotationY, rotationZ);
        // rotationEuler = makeVec3LinearFromArrays("rotate", timesArray, rotationX, rotationY, rotationZ);
    }
}

void MViewParser::AnimatedObject::copyFromExtractedKeys(const std::string& keyName, osg::ref_ptr<osg::FloatArray>& timesArray, osg::ref_ptr<osg::FloatArray>& keyArray)
{
    std::vector<std::pair<int, float>> extractedKeys = extractKeyframes(*(animatedPropertiesMap[keyName]));

    for (auto& extractedKey : extractedKeys)
    {
        (*timesArray)[extractedKey.first] = static_cast<float>(extractedKey.first) / static_cast<float>(modelPartFPS);
        (*keyArray)[extractedKey.first] = extractedKey.second;
    }
}

osg::ref_ptr<osgAnimation::Vec3LinearChannel> MViewParser::AnimatedObject::makeVec3LinearFromArrays(const std::string channelName,
    osg::ref_ptr<osg::FloatArray>& timesArray, osg::ref_ptr<osg::FloatArray>& keyXArray, osg::ref_ptr<osg::FloatArray>& keyYArray,
    osg::ref_ptr<osg::FloatArray>& keyZArray)
{
    osg::ref_ptr<osgAnimation::Vec3LinearChannel> channel = new osgAnimation::Vec3LinearChannel();
    channel->setName(channelName);
    channel->setTargetName(partName);

    //if (timesArray->getNumElements() > 0)
    //{
    //    channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->reserve(timesArray->getNumElements() + 1);
    //    osgAnimation::Vec3Keyframe dummy;
    //    osg::Vec3 vec = channelName == "scale" ? osg::Vec3(1.0, 1.0, 1.0) : osg::Vec3();
    //    dummy.setTime(0);
    //    dummy.setValue(vec);
    //    channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->push_back(dummy);
    //}

    for (unsigned int i = 0; i < timesArray->getNumElements(); ++i)
    {
        osgAnimation::Vec3Keyframe f;
        osg::Vec3 vec((*keyXArray)[i], (*keyYArray)[i], (*keyZArray)[i]);

        f.setTime((*timesArray)[i]);
        f.setValue(vec);

        channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->push_back(f);
    }

    return channel;
}

osg::ref_ptr<osgAnimation::QuatSphericalLinearChannel> MViewParser::AnimatedObject::makeQuatLinearFromArrays(const std::string channelName, 
    osg::ref_ptr<osg::FloatArray>& timesArray, osg::ref_ptr<osg::FloatArray>& keyXArray, osg::ref_ptr<osg::FloatArray>& keyYArray, 
    osg::ref_ptr<osg::FloatArray>& keyZArray)
{
    osg::ref_ptr<osgAnimation::QuatSphericalLinearChannel> channel = new osgAnimation::QuatSphericalLinearChannel();

    channel->setName(channelName);
    channel->setTargetName(partName);

    channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->reserve(timesArray->getNumElements() + 1);
    
    //if (timesArray->getNumElements() > 0)
    //{
    //    osgAnimation::QuatKeyframe dummy;
    //    dummy.setTime(0);
    //    dummy.setValue(osg::Quat());
    //    channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->push_back(dummy);
    //}

    std::vector<osg::Vec3> rotateTempArray;
    for (unsigned int i = 0; i < timesArray->getNumElements(); ++i)
    {
        osgAnimation::QuatKeyframe f;
        osg::Quat quat(osg::DegreesToRadians((*keyXArray)[i]), osg::X_AXIS,
            osg::DegreesToRadians((*keyYArray)[i]), osg::Y_AXIS,
            osg::DegreesToRadians((*keyZArray)[i]), osg::Z_AXIS);

        f.setTime((*timesArray)[i]);
        f.setValue(quat);

        channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->push_back(f);

        rotateTempArray.push_back(osg::Vec3((*keyXArray)[i], (*keyYArray)[i], (*keyZArray)[i]));
    }

    return channel;
}

MViewParser::Animation::Animation(const MViewFile::Archive& archive, const nlohmann::json& description)
{
    name = description.value("name", "");
    expectedNumAnimatedObjects = description.value("numAnimatedObjects", 0);

    if (description.contains("animatedObjects"))
    {
        int id = 0;
        for (auto& animatedObject : description["animatedObjects"])
        {
            animatedObjects.push_back(AnimatedObject(archive, animatedObject, id++));
        }
    }
}

const osg::ref_ptr<osgAnimation::Animation> MViewParser::Animation::asAnimation()
{
    osg::ref_ptr<osgAnimation::Animation> animation = new osgAnimation::Animation();

    animation->setName(name);

    for (auto& animatedObject : animatedObjects)
    {
        if (// animatedObject.sceneObjectType == "MeshSO" || 
            (animatedObject.sceneObjectType == "Node" && animatedObject.skinningRigIndex == -1 && animatedObject.parentIndex == 0))
        {
            if (animatedObject.translation && animatedObject.translation->getOrCreateSampler()->getKeyframeContainer()->size() > 0)
                animation->getChannels().push_back(animatedObject.translation);

            if (animatedObject.rotation && animatedObject.rotation->getOrCreateSampler()->getKeyframeContainer()->size() > 0)
                animation->getChannels().push_back(animatedObject.rotation);

            if (animatedObject.scale && animatedObject.scale->getOrCreateSampler()->getKeyframeContainer()->size() > 0)
                animation->getChannels().push_back(animatedObject.scale);

            //if (animatedObject.rotationEuler && animatedObject.rotationEuler->getOrCreateSampler()->getKeyframeContainer()->size() > 1)
            //    animation->getChannels().push_back(animatedObject.rotationEuler);

        }
    }


    return animation;
}

