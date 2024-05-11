#include "pch.h"

#include <stdlib.h>
#include <limits>
#include <string>
#include <vector>

#include <osg/MatrixTransform>
#include <osg/Geode>
#include <osg/Vec3f>

#include <osg/Geometry>
#include <osg/StateSet>
#include <osg/TexGen>
#include <osg/TexMat>

#include <osg/TextureRectangle>
#include <osg/Texture2D>
#include <osg/Texture1D>
#include <osg/Types>
#include <osg/Material>
#include <osg/BlendFunc>

#include <osg/UserDataContainer>

#include <osgText/Text>

#include <osgAnimation/MorphGeometry>

#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>

#include <osgSim/ShapeAttribute>

#include "OsgjsFileCache.h"
#include "OsgjsParser.h"
#include "OsgjsParserHelper.h"


using json = nlohmann::json;

// Callback processing function map
using namespace osg;
using namespace osgAnimation;
using namespace osgJSONParser;


// PUBLIC

ref_ptr<Group> OsgjsParser::parseObjectTree(const json& firstOsgNodeJSON)
{
    ref_ptr<Group> rootNode = new Group;

    rootNode->setName("OSGJS-Imported-Scene");

    buildMaterialAndtextures();

    OSG_NOTICE << "Parsing Scene tree..." << std::endl;
    if (parseObject(rootNode, firstOsgNodeJSON, "JSON Root"))
        return rootNode;
    else
        return nullptr;
}


// PRIVATE

#define ADD_NODE_KEY std::string("[Object: ") + nodeKey + std::string("]")
#define ADD_NODE_NAME (currentJSONNode.contains("Name") ? ("[Name: " + std::string(currentJSONNode["Name"]) + "]") : std::string(""))
#define ADD_UNIQUE_ID (currentJSONNode.contains("UniqueID") ? ("[UniqueID: " + std::to_string(currentJSONNode["UniqueID"].get<int>()) + "]") : std::string(""))
#define ADD_KEY_NAME ADD_NODE_KEY << ADD_NODE_NAME << ADD_UNIQUE_ID

constexpr auto MODELINFO_FILE = "model_info.json";

void OsgjsParser::buildMaterialAndtextures() 
{
    std::string viewerInfoFile = _filesBasePath.empty() ? std::string("viewer_info.json") : _filesBasePath + std::string("\\viewer_info.json");
    std::string textureInfoFile = _filesBasePath.empty() ? std::string("texture_info.json") : _filesBasePath + std::string("\\texture_info.json");
    if (!_meshMaterials2.readMaterialFile(viewerInfoFile, textureInfoFile))
    {
        OSG_NOTICE << "INFO: Could not read '" << viewerInfoFile << "' or '" << textureInfoFile << "'. Models will be exported without textures." << std::endl;
        return;
    }

    std::map<std::string, TextureInfo2> textureMap = _meshMaterials2.getTextureMap();
    OSG_NOTICE << "Resolving scene textures... [" << textureMap.size() << "]" << std::endl;
    
    createTextureMap(textureMap);
}

void OsgjsParser::lookForChildren(ref_ptr<Object> object, const json& currentJSONNode, UserDataContainerType containerType, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    // Lookup current node horizontally, searching for JSON Objects to process and attach to newObject
    if (currentJSONNode.contains("Children") && currentJSONNode["Children"].is_array())
    {
        for (const auto& child : currentJSONNode["Children"])
        {
            if (!parseObject(object, child, nodeKey))
                OSG_WARN << "WARNING: object " << object->getName() + " had not parseable children. ->" << std::endl << ADD_KEY_NAME << std::endl;
        }
    }

    // Get User Data Containers for object
    if (currentJSONNode.contains("UserDataContainer"))
        parseUserDataContainer(object, currentJSONNode["UserDataContainer"], containerType, nodeKey);

    // Get object state set
    if (currentJSONNode.contains("StateSet"))
        parseStateSet(object, currentJSONNode["StateSet"]["osg.StateSet"], "osg.StateSet");

    // Get UpdateCallbacks for animations
    if (currentJSONNode.contains("UpdateCallbacks") && currentJSONNode["UpdateCallbacks"].is_array())
    {
        for (const auto& child : currentJSONNode["UpdateCallbacks"])
        {
            ref_ptr<Callback> newCallback = new Callback;
            if (!parseCallback(newCallback, child, nodeKey) || dynamic_pointer_cast<Node>(object) == false)
            {
                OSG_WARN << "WARNING: Could not apply animation callbacks to object. ->" << std::endl << ADD_KEY_NAME << std::endl;
                break;
            }
            else
                dynamic_pointer_cast<Node>(object)->addUpdateCallback(newCallback);
        }
    }
}

bool OsgjsParser::parseObject(ref_ptr<Object> currentObject, const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentObject ? currentObject->getName() : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    if (currentJSONNode.is_object())
    {
        // Create a new node to accomodate the object
        ref_ptr<Object> newObject;

        // Lookup current node vertically, searching for JSON Objects to process and transform into newObject
        for (auto itr = currentJSONNode.begin(); itr != currentJSONNode.end(); ++itr)
        {
            auto found = processObjects.find(itr.key());
            if (found != processObjects.end() && itr.value().is_object())
            {
                newObject = found->second(itr.value(), itr.key());
            }
            else if (found != processObjects.end() && !itr.value().is_object())
            {
                OSG_WARN << " found a Object JSON node [" << itr.key() <<
                    "] that is not an object or is malformed." << ADD_KEY_NAME << std::endl;
            }
        }

        // In case no object has been created yet, we create a default new Group
        if (!newObject)
            newObject = new Group;

        // Look for any children
        lookForChildren(newObject, currentJSONNode, UserDataContainerType::UserData, nodeKey);

        // Try to append created object to tree
        assert(dynamic_pointer_cast<Group>(currentObject));
        if (dynamic_pointer_cast<Geode>(currentObject))
        {
            assert(newObject->asDrawable());
            if (newObject->asDrawable())
                dynamic_pointer_cast<Geode>(currentObject)->addDrawable(newObject->asDrawable());
            else
                OSG_WARN << "WARNING: Could not find Drawable geometry in Geode node!" << ADD_KEY_NAME << std::endl;
        }
        else if (dynamic_pointer_cast<Group>(currentObject))
            dynamic_pointer_cast<Group>(currentObject)->addChild(reinterpret_cast<Node*>(newObject.get()));
        else
        {
            OSG_FATAL << "Something went wrong and object tree is broken!" << ADD_KEY_NAME << std::endl;
            return false;
        }

        return true;
    }

    return false;
}

bool OsgjsParser::parseCallback(ref_ptr<Callback> currentCallback, const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    if (currentJSONNode.is_object())
    {
        // Create a new node to accomodate the object
        ref_ptr<Callback> newCallback;

        // Lookup current node vertically, searching for JSON Callbacks to process
        for (auto itr = currentJSONNode.begin(); itr != currentJSONNode.end(); ++itr)
        {
#ifndef NDEBUG
            std::string debugNodeKey = itr.key();
            std::string debugNodeValue = itr.value().dump();
            int debugNodeUniqueID = itr.value().contains("UniqueID") ? itr.value()["UniqueID"].get<int>() : -1;
            debugNodeUniqueID = debugNodeUniqueID;
#endif
            auto found = processCallbacks.find(itr.key());
            if (found != processCallbacks.end() && itr.value().is_object())
            {
                newCallback = found->second(itr.value(), itr.key());
            }
            else if (found != processCallbacks.end() && !itr.value().is_object())
            {
                notify(osg::DEBUG_INFO) << " found a Callback JSON node [" << itr.key() <<
                    "] that is not a callback or is malformed." << ADD_KEY_NAME << std::endl;
                return false;
            }
        }

        if (!newCallback)
        {
            notify(osg::DEBUG_INFO) << "Could not process current JSON node: " <<
                (currentCallback ? currentCallback->getName() : "") << ADD_KEY_NAME << std::endl;
            return false;
        }

        currentCallback->addNestedCallback(newCallback);
        return true;
    }

    return false;
}

void OsgjsParser::parseUserDataContainer(ref_ptr<Object> currentObject, const json& currentJSONNode, UserDataContainerType containerType, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";

    switch (containerType)
    {
    case UserDataContainerType::None:
    {
        OSG_WARN << "Container for current object has no specification!" << ADD_KEY_NAME << std::endl;
        break;
    }
    case UserDataContainerType::UserData:
    {
        const DefaultUserDataContainer* oldUdc = dynamic_cast<DefaultUserDataContainer*>(currentObject->getUserDataContainer());
        ref_ptr<DefaultUserDataContainer> udc;
        if (!oldUdc)
        {
            udc = new DefaultUserDataContainer;
        }
        else
            udc = new DefaultUserDataContainer(*oldUdc);

        udc->setName(name);

        if (currentJSONNode["Values"].is_array())
        {
            for (auto& data : currentJSONNode["Values"])
            {
                if (data.is_object())
                {
                    ref_ptr<TemplateValueObject<std::string>> obj = new TemplateValueObject<std::string>;
                    obj->setName(data["Name"]);
                    obj->setValue(data["Value"]);
                    udc->addUserObject(obj);
                }
            }
        }

        currentObject->setUserDataContainer(udc);
        break;
    }
    case UserDataContainerType::ShapeAttributes:
    {
        ref_ptr<osgSim::ShapeAttributeList> shapeAttrList = new osgSim::ShapeAttributeList;
        shapeAttrList->setName(name);

        if (currentJSONNode["Values"].is_array())
        {
            for (auto& data : currentJSONNode["Values"])
            {
                if (data.is_object())
                {
                    osgSim::ShapeAttribute shapeAttr;
                    shapeAttr.setName(data["Name"]);

                    int vari = 0;
                    double vard = 0.0;

                    if (ParserHelper::getSafeInteger(data["Value"], vari))
                        shapeAttr.setValue(vari);
                    else if (ParserHelper::getSafeDouble(data["Value"], vard))
                        shapeAttr.setValue(vard);
                    else
                        shapeAttr.setValue(data["Value"].get<std::string>().c_str());
                    shapeAttrList->push_back(shapeAttr);
                }
            }
        }

        currentObject->setUserData(shapeAttrList);
        break;
    }
    }
}

void OsgjsParser::parseStateSet(ref_ptr<Object> currentObject, const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<StateSet> stateset = new StateSet;

    if (currentJSONNode.contains("RenderingHint"))
    {
        stateset->setRenderingHint(StateSet::TRANSPARENT_BIN);
    }

    // Parse texture attributes
    if (currentJSONNode.contains("TextureAttributeList") && currentJSONNode["TextureAttributeList"].is_array())
    {
        int i = 0;
        for (const auto& child : currentJSONNode["TextureAttributeList"])
        {
            if (child.is_array()) // TextureAttributeList use double arrays, but with 1 object each subarray.
            {
                for (const auto& childChild : child)
                {
                    // Find subobjects on children nodes - Must be texture objects.
                    for (auto itr = childChild.begin(); itr != childChild.end(); ++itr)
                    {
                        ref_ptr<Object> childTexture;
                        auto found = processObjects.find(itr.key());
                        if (found != processObjects.end() && itr.value().is_object())
                        {
                            childTexture = found->second(itr.value(), itr.key());
                        }
                        else if (found != processObjects.end() && !itr.value().is_object())
                        {
                            OSG_WARN << " found a Object JSON node [" << itr.key() <<
                                "] that is not an object or is malformed." << ADD_KEY_NAME << std::endl;
                        }

                        if (childTexture && !dynamic_pointer_cast<Texture>(childTexture))
                        {
                            OSG_WARN << "WARNING: invalid texture. " << ADD_KEY_NAME
                                << "[Subkey: " << itr.key()
                                << (itr.value().contains("Name") ? ("[Name: " + itr.value()["Name"].get<std::string>() + "]") : "")
                                << std::endl;
                        }
                        else if (childTexture)
                        {
                            stateset->setTextureAttribute(i, dynamic_pointer_cast<Texture>(childTexture), StateAttribute::TEXTURE);
                        }
                        ++i;
                    }
                }
            }
        }
    }

    // Parse other attributes. Currently: osg.Material, osg.BlendFunc, osg.CullFace, osg.BlendColor, 
    if (currentJSONNode.contains("AttributeList") && currentJSONNode["AttributeList"].is_array())
    {
        for (const auto& child : currentJSONNode["AttributeList"])
        {
            // Find subobjects on children nodes - Must be geometry objects.
            int i = 0;
            for (auto itr = child.begin(); itr != child.end(); ++itr)
            {
                ref_ptr<Object> childState;
                auto found = processObjects.find(itr.key());
                if (found != processObjects.end() && itr.value().is_object())
                {
                    childState = found->second(itr.value(), itr.key());
                }
                else if (found != processObjects.end() && !itr.value().is_object())
                {
                    OSG_WARN << " found a Object JSON node [" << itr.key() <<
                        "] that is not an object or is malformed. " << ADD_KEY_NAME << std::endl;
                }

                if (childState && !dynamic_pointer_cast<StateAttribute>(childState))
                {
                    OSG_WARN << "WARNING: invalid StateAttribute. " << ADD_KEY_NAME
                        << "[Subkey: " << itr.key()
                        << (itr.value().contains("Name") ? ("[Name: " + itr.value()["Name"].get<std::string>() + "]") : "")
                        << std::endl;
                }
                else if (childState)
                {
                    if (dynamic_pointer_cast<Material>(childState))
                        stateset->setAttribute(dynamic_pointer_cast<Material>(childState), StateAttribute::MATERIAL);
                    else if (dynamic_pointer_cast<BlendFunc>(childState))
                        stateset->setAttribute(dynamic_pointer_cast<BlendFunc>(childState), StateAttribute::BLENDFUNC);
                    else if (dynamic_pointer_cast<BlendColor>(childState))
                        stateset->setAttribute(dynamic_pointer_cast<BlendColor>(childState), StateAttribute::BLENDCOLOR);
                    else if (dynamic_pointer_cast<CullFace>(childState))
                        stateset->setAttribute(dynamic_pointer_cast<CullFace>(childState), StateAttribute::CULLFACE);
                }
                ++i;
            }
        }
    }

    // Custom step: try to get textures from MaterialInfo and set on User Values of materials
    postProcessStateSet(stateset, &currentJSONNode);

    // Apply stateset.
    if (dynamic_pointer_cast<Node>(currentObject))
        dynamic_pointer_cast<Node>(currentObject)->setStateSet(stateset);
    else
        OSG_WARN << "WARNING: Object has stateset but isn't subclass of Node. " << ADD_KEY_NAME << std::endl;
}



ref_ptr<Object> OsgjsParser::parseOsgNode(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";

    // Look ahead to see if this ogs.Node contains only geometry nodes
    bool isGeode = false;
    if (currentJSONNode.contains("Children") && currentJSONNode["Children"].is_array())
    {
        for (auto& child : currentJSONNode["Children"])
        {
            if (!child.is_object())
            {
                isGeode = false;
                break;
            }

            for (auto itr = child.begin(); itr != child.end(); itr++)
            {
                if (drawableNodes.find(itr.key()) != drawableNodes.end())
                    isGeode = true;
                else
                {
                    // Override if found previously and quit search
                    isGeode = false;
                    break;
                }
            }
        }
    }

    // Create a group node
    ref_ptr<Group> newObject;
    if (isGeode)
    {
        newObject = new Geode;
    }
    else
        newObject = new Group;

    // Add name information to node
    newObject->setName(name);

    lookForChildren(newObject, currentJSONNode, isGeode ? UserDataContainerType::ShapeAttributes : UserDataContainerType::UserData, nodeKey);

    // If a Node has a stateset (grabbed in lookForChildren) with a material, applies the same material to all children geometry
    osg::StateSet* meshState = newObject->getStateSet();
    if (meshState)
    {
        const osg::Material* mat = dynamic_cast<const osg::Material*>(meshState->getAttribute(osg::StateAttribute::MATERIAL));
        if (mat && newObject->getNumChildren() > 0)
        {
            std::string matName = mat->getName();
            CascadeMaterials(newObject, matName);
        }
    }

    return newObject;
}



ref_ptr<Object> OsgjsParser::parseOsgMatrixTransform(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    // Create a matrix transform
    ref_ptr<MatrixTransform> newObject;

    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";

    if (nodeKey == "osg.MatrixTransform")
        newObject = new MatrixTransform;
    else if (nodeKey == "osgAnimation.Skeleton")
        newObject = new Skeleton;
    else // if (nodeKey == "osgAnimation.Bone")
        newObject = new Bone;

    // Object helpers
    Skeleton* skeleton = dynamic_pointer_cast<Skeleton>(newObject);
    Bone* bone = dynamic_pointer_cast<Bone>(newObject);

    // Add name information to node
    newObject->setName(name);

    // Get the matrix
    if (!currentJSONNode.contains("Matrix") || !currentJSONNode["Matrix"].is_array() || currentJSONNode["Matrix"].size() != 16)
    {
        OSG_DEBUG << "DEBUG: MatrixTransform's Matrix object does not exist or have incorrect size!" << ADD_KEY_NAME << std::endl;
    }
    else
    {
        Matrix matrix;
        int index = 0;
        for (auto& value : currentJSONNode["Matrix"])
        {
            matrix(index / 4, index % 4) = value.get<double>();
            ++index;
        }

        if (_firstMatrix)
        {
            // Add custom information of first matrix for exporters
            newObject->setUserValue("firstMatrix", true);
            _firstMatrix = false;

            // Add model info to mesh
            std::string modelName = getModelName();
            if (!modelName.empty())
            {
                newObject->setName(modelName);
            }
        }

        newObject->setMatrix(matrix);
    }

    // Bone processing
    if (bone)
    {
        if (currentJSONNode.contains("InvBindMatrixInSkeletonSpace") && currentJSONNode["InvBindMatrixInSkeletonSpace"].is_array() &&
            currentJSONNode["InvBindMatrixInSkeletonSpace"].size() == 16)
        {
            Matrix matrix;
            int index = 0;
            for (auto& value : currentJSONNode["InvBindMatrixInSkeletonSpace"])
            {
                matrix(index / 4, index % 4) = value.get<double>();
                ++index;
            }

            bone->setInvBindMatrixInSkeletonSpace(matrix);
        }

        if (currentJSONNode.contains("BoundingBox") && currentJSONNode["BoundingBox"].is_object())
        {
            const json& boundingBox = currentJSONNode["BoundingBox"];

            Vec3 min(0.0, 0.0, 0.0), max(0.0, 0.0, 0.0);
            if (boundingBox.contains("min") && boundingBox["min"].is_array() && boundingBox["min"].size() == 3)
                min = { boundingBox["min"][0].get<float>(), boundingBox["min"][1].get<float>(), boundingBox["min"][2].get<float>() };
            if (boundingBox.contains("max") && boundingBox["max"].is_array() && boundingBox["max"].size() == 3)
                max = { boundingBox["max"][0].get<float>(), boundingBox["max"][1].get<float>(), boundingBox["max"][2].get<float>() };

            bone->setUserValue("AABBonBone_min", min);
            bone->setUserValue("AABBonBone_max", max);
        }
        bone->setDataVariance(osg::Object::DYNAMIC);
    }

    if (skeleton)
        skeleton->setDataVariance(osg::Object::DYNAMIC);

    // Get object children
    lookForChildren(newObject, currentJSONNode, UserDataContainerType::UserData, nodeKey);

    // Update existing currentObject
    return newObject;
}

ref_ptr<Object> OsgjsParser::parseOsgGeometry(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<Geometry> newGeometry;

    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";

    // Polymorph based on node key
    if (nodeKey == "osg.Geometry")
        newGeometry = new Geometry;
    else if (nodeKey == "osgAnimation.MorphGeometry")
        newGeometry = new MorphGeometry;
    else if (nodeKey == "osgAnimation.RigGeometry")
        newGeometry = new RigGeometry;
    else
    {
        OSG_WARN << "WARNING: Unknown geometry node!" << ADD_KEY_NAME << std::endl;
        return nullptr;
    }

    // Helper pointers
    RigGeometry* rigGeometry = dynamic_pointer_cast<RigGeometry>(newGeometry);
    MorphGeometry* morphGeometry = dynamic_pointer_cast<MorphGeometry>(newGeometry);

    // Add name information to node
    newGeometry->setName(name);

    const json* vertexAttributeList = currentJSONNode.contains("VertexAttributeList") ? &currentJSONNode["VertexAttributeList"] : nullptr;
    const json* vertexNode = nullptr;
    const json* normalNode = nullptr;
    const json* colorNode = nullptr;
    const json* tangentNode = nullptr;
    const json* bonesNode = nullptr;
    const json* weightsNode = nullptr;

    std::map<int, const json*> texCoordNodes;

    ref_ptr<Array> vertices;
    ref_ptr<Array> normals;
    ref_ptr<Array> colors;
    ref_ptr<Array> tangents;
    ref_ptr<Array> bones;
    ref_ptr<Array> weights;
    std::map<int, ref_ptr<Array>> texcoords;
    ref_ptr<Array> indices;
    uint32_t magic = 0;
    GLenum drawMode = GL_POINTS;

    // 1) Get object statesets and userData
    lookForChildren(newGeometry, currentJSONNode, UserDataContainerType::ShapeAttributes, nodeKey);

    // 2) Parse Vertex Attributes List
    if (currentJSONNode.contains("VertexAttributeList") && currentJSONNode["VertexAttributeList"].is_object())
    {
        // 2.1) Get VertexAttributeList members

        if (vertexAttributeList->contains("Vertex") && (*vertexAttributeList)["Vertex"].is_object())
            vertexNode = &(*vertexAttributeList)["Vertex"];
        if (vertexAttributeList->contains("Normal") && (*vertexAttributeList)["Normal"].is_object())
            normalNode = &(*vertexAttributeList)["Normal"];
        if (vertexAttributeList->contains("Color") && (*vertexAttributeList)["Color"].is_object())
            colorNode = &(*vertexAttributeList)["Color"];
        if (vertexAttributeList->contains("Tangent") && (*vertexAttributeList)["Tangent"].is_object())
            tangentNode = &(*vertexAttributeList)["Tangent"];
        if (vertexAttributeList->contains("Bones") && (*vertexAttributeList)["Bones"].is_object())
            bonesNode = &(*vertexAttributeList)["Bones"];
        if (vertexAttributeList->contains("Weights") && (*vertexAttributeList)["Weights"].is_object())
            weightsNode = &(*vertexAttributeList)["Weights"];

        std::stringstream ss;
        for (int i = 0; i < 32; i++)
        {
            ss.str("");
            ss << "TexCoord" << i;
            if (vertexAttributeList->contains(ss.str()) && (*vertexAttributeList)[ss.str()].is_object())
                texCoordNodes[i] = &(*vertexAttributeList)[ss.str()];
        }

        // 2.2) Get VertexAttributeList arrays
#ifndef NDEBUG
        std::string vertexNodestr = vertexNode ? vertexNode->dump() : "";
        std::string normalNodestr = normalNode ? normalNode->dump(): "";
        std::string colorNodestr = colorNode ? colorNode->dump() : "";
        std::string tangentNodestr = tangentNode ? tangentNode->dump() : "";
        std::string bonesNodestr = bonesNode ? bonesNode->dump() : "";
        std::string weightsNodestr = weightsNode ? weightsNode->dump() : "";
#endif

        if (vertexNode && vertexNode->contains("Array") && (*vertexNode)["Array"].is_object() && vertexNode->contains("ItemSize") && (*vertexNode)["ItemSize"].is_number())
            vertices = ParserHelper::parseJSONArray((*vertexNode)["Array"], (*vertexNode)["ItemSize"].get<int>(), _fileCache, magic);
        if (normalNode && normalNode->contains("Array") && (*normalNode)["Array"].is_object() && normalNode->contains("ItemSize") && (*normalNode)["ItemSize"].is_number())
            normals = ParserHelper::parseJSONArray((*normalNode)["Array"], (*normalNode)["ItemSize"].get<int>(), _fileCache, magic);
        if (colorNode && colorNode->contains("Array") && (*colorNode)["Array"].is_object() && colorNode->contains("ItemSize") && (*colorNode)["ItemSize"].is_number())
            colors = ParserHelper::parseJSONArray((*colorNode)["Array"], (*colorNode)["ItemSize"].get<int>(), _fileCache, magic);
        if (tangentNode && tangentNode->contains("Array") && (*tangentNode)["Array"].is_object() && tangentNode->contains("ItemSize") && (*tangentNode)["ItemSize"].is_number())
            tangents = ParserHelper::parseJSONArray((*tangentNode)["Array"], (*tangentNode)["ItemSize"].get<int>(), _fileCache, magic);
        if (bonesNode && bonesNode->contains("Array") && (*bonesNode)["Array"].is_object() && bonesNode->contains("ItemSize") && (*bonesNode)["ItemSize"].is_number())
            bones = ParserHelper::parseJSONArray((*bonesNode)["Array"], (*bonesNode)["ItemSize"].get<int>(), _fileCache, magic);
        if (weightsNode && weightsNode->contains("Array") && (*weightsNode)["Array"].is_object() && weightsNode->contains("ItemSize") && (*weightsNode)["ItemSize"].is_number())
            weights = ParserHelper::parseJSONArray((*weightsNode)["Array"], (*weightsNode)["ItemSize"].get<int>(), _fileCache, magic);

        for (auto& texCoordNode : texCoordNodes)
        {
            if (texCoordNode.second->contains("Array") && (*texCoordNode.second)["Array"].is_object() && texCoordNode.second->contains("ItemSize") && 
                (*texCoordNode.second)["ItemSize"].is_number())
                texcoords[texCoordNode.first] = ParserHelper::parseJSONArray((*texCoordNode.second)["Array"], (*texCoordNode.second)["ItemSize"].get<int>(), _fileCache, magic);
        }

        // 2.3) Sanity checks
        if (nodeKey == "osg.Geometry")
        {
            if (vertices)
            {
                if (vertices->getNumElements() == 0)
                {
                    OSG_WARN << "WARNING: Model contains a geometry node without any vertices. Ignoring..." << ADD_KEY_NAME << std::endl;
                    return newGeometry;
                }
                if (normals && vertices->getNumElements() != normals->getNumElements())
                {
                    OSG_WARN << "WARNING: Model contains normals that don't match number of vertices..." << ADD_KEY_NAME << std::endl;
                }
                if (tangents && vertices->getNumElements() != tangents->getNumElements())
                {
                    OSG_WARN << "WARNING: Model contains tangents that don't match number of vertices..." << ADD_KEY_NAME << std::endl;
                }
                if (colors && vertices->getNumElements() != colors->getNumElements())
                {
                    OSG_WARN << "WARNING: Model contains colors that don't match number of vertices..." << ADD_KEY_NAME << std::endl;
                }
                bool texError = false;
                for (auto& texcoordcheck : texcoords)
                    if (vertices->getNumElements() != texcoordcheck.second->getNumElements())
                        texError = true;

                if (texError)
                    OSG_WARN << "WARNING: Model contain 1 or more texCoords that don't match number of vertices..." << ADD_KEY_NAME << std::endl;
            }
        }


        // 2.4) Set Geometry Attributes

        if (vertices)
            newGeometry->setVertexArray(vertices);
        if (normals)
            newGeometry->setNormalArray(normals, Array::BIND_PER_VERTEX);
        if (colors)
            newGeometry->setColorArray(colors, Array::BIND_PER_VERTEX);
        if (tangents)
        {
            tangents->setUserValue("tangent", true);
            newGeometry->setVertexAttribArray(newGeometry->getVertexAttribArrayList().size(), tangents);
        }

        for (auto& texcoord : texcoords)
        {
            newGeometry->setTexCoordArray(texcoord.first, texcoord.second);
        }
    }

    // 3) Parse Primitive Set List
    if (currentJSONNode.contains("PrimitiveSetList") && currentJSONNode["PrimitiveSetList"].is_array())
    {
        const json& primitiveSetList = currentJSONNode["PrimitiveSetList"];
        for (auto& primitiveSet : primitiveSetList)
        {
            ref_ptr<PrimitiveSet> newPrimitiveSet;
            const json* newDrawElementNode = nullptr;

            if (primitiveSet.contains("DrawElementsUInt") && primitiveSet["DrawElementsUInt"].is_object()) {
                newPrimitiveSet = new DrawElementsUInt;
                newDrawElementNode = &primitiveSet["DrawElementsUInt"];
            }
            else if (primitiveSet.contains("DrawElementsUShort") && primitiveSet["DrawElementsUShort"].is_object()) {
                newPrimitiveSet = new DrawElementsUShort;
                newDrawElementNode = &primitiveSet["DrawElementsUShort"];
            }
            else if (primitiveSet.contains("DrawElementsUByte") && primitiveSet["DrawElementsUByte"].is_object()) {
                newPrimitiveSet = new DrawElementsUByte;
                newDrawElementNode = &primitiveSet["DrawElementsUByte"];
            }
            else if (primitiveSet.contains("DrawArrayLengths") && primitiveSet["DrawArrayLengths"].is_object()) {
                newPrimitiveSet = new DrawArrayLengths;
                newDrawElementNode = &primitiveSet["DrawArrayLengths"];
            }
            else if (primitiveSet.contains("DrawArrays") && primitiveSet["DrawArrays"].is_object()) {
                newPrimitiveSet = new DrawArrays;
                newDrawElementNode = &primitiveSet["DrawArrays"];
            }
            else {
                OSG_WARN << "WARNING: Unsuported primitive type. Skipping." << std::endl;
                continue;
            }

            if (newPrimitiveSet && newDrawElementNode)
            {
                // Parse Draw modes
                if ((*newDrawElementNode).contains("Mode"))
                {
                    drawMode = ParserHelper::getModeFromString((*newDrawElementNode)["Mode"].get<std::string>());
                    newPrimitiveSet->setMode(drawMode);
                }

                // Process DrawElement objects
                if ((*newDrawElementNode).contains("Indices") && (*newDrawElementNode)["Indices"].is_object())
                {
                    const json& newPrimitiveIndices = (*newDrawElementNode)["Indices"];
                    if (newPrimitiveIndices.contains("Array") && newPrimitiveIndices["Array"].is_object() && newPrimitiveIndices.contains("ItemSize") && newPrimitiveIndices["ItemSize"].is_number())
                    {
                        // Get parameters to check if need decode indices
                        bool needDecodeIndices(false);
                        ref_ptr<osgSim::ShapeAttributeList> shapeAttrList = dynamic_cast<osgSim::ShapeAttributeList*>(newGeometry->getUserData());
                        if (shapeAttrList)
                        {
                            double temp;
                            const std::vector<std::string> vertexAttributes = { "attributes", "vertex_bits", "vertex_mode", "epsilon", "nphi", "triangle_mode"};
                            for (auto& verteAttribute : vertexAttributes)
                            {
                                needDecodeIndices = ParserHelper::getShapeAttribute(shapeAttrList, verteAttribute, temp);
                                if (needDecodeIndices)
                                    break;
                            }
                        }

                        indices = ParserHelper::parseJSONArray(newPrimitiveIndices["Array"], 
                            newPrimitiveIndices["ItemSize"].get<int>(), _fileCache, magic, needDecodeIndices, drawMode);

                        if (indices)
                        {
                            DrawElementsUInt* dei = dynamic_cast<DrawElementsUInt*>(newPrimitiveSet.get());
                            DrawElementsUShort* des = dynamic_cast<DrawElementsUShort*>(newPrimitiveSet.get());
                            DrawElementsUByte* deb = dynamic_cast<DrawElementsUByte*>(newPrimitiveSet.get());

                            if (dei)
                                dei->insert(dei->begin(), dynamic_cast<UIntArray*>(indices.get())->begin(), 
                                    dynamic_cast<UIntArray*>(indices.get())->end());
                            else if (des)
                                des->insert(des->begin(), dynamic_cast<UShortArray*>(indices.get())->begin(),
                                    dynamic_cast<UShortArray*>(indices.get())->end());
                            else if (deb)
                                deb->insert(deb->begin(), dynamic_cast<UByteArray*>(indices.get())->begin(),
                                    dynamic_cast<UByteArray*>(indices.get())->end());
                        }
                    }
                }

                auto da = dynamic_cast<DrawArrays*>(newPrimitiveSet.get());
                if (da && (*newDrawElementNode).contains("First") && (*newDrawElementNode).contains("Count"))
                {
                    da->setFirst((*newDrawElementNode)["First"].get<int>());
                    da->setCount((*newDrawElementNode)["Count"].get<int>());
                }
                
                // Process DrawArrayLengths
                auto dal = dynamic_cast<DrawArrayLengths*>(newPrimitiveSet.get());
                if (dal && (*newDrawElementNode).contains("ArrayLengths") && (*newDrawElementNode)["ArrayLengths"].is_array())
                {
                    if ((*newDrawElementNode).contains("First"))
                        dal->setFirst((*newDrawElementNode)["First"].get<int>());
                    if ((*newDrawElementNode).contains("Mode"))
                        dal->setMode(ParserHelper::getModeFromString((*newDrawElementNode)["Mode"].get<std::string>()));

                    dal->reserve((*newDrawElementNode)["ArrayLengths"].size());
                    for (auto& value : (*newDrawElementNode)["ArrayLengths"])
                    {
                        dal->push_back(value.get<int>());
                    }
                }
            }

            // Append primitive
            if (newPrimitiveSet)
                newGeometry->addPrimitiveSet(newPrimitiveSet);
        }
    }

    // 4) Get ComputBoundingBoxCallback
    if (currentJSONNode.contains("osg.ComputeBoundingBoxCallback") && currentJSONNode["osg.ComputeBoundingBoxCallback"].is_object())
    {
        std::ignore = parseComputeBoundingBoxCallback(currentJSONNode["osg.ComputeBoundingBoxCallback"], "osg.ComputeBoundingBoxCallback");
    }

    // 5) Morph Geometry processing
    if (nodeKey == "osgAnimation.MorphGeometry")
    {
        assert(morphGeometry);

        if (currentJSONNode.contains("MorphTargets") && currentJSONNode["MorphTargets"].is_array())
        {
            for (const auto& child : currentJSONNode["MorphTargets"])
            {
                // Find subobjects on children nodes - Must be geometry objects.
                for (auto itr = child.begin(); itr != child.end(); ++itr)
                {
                    ref_ptr<Object> childGeometry;
                    auto found = processObjects.find(itr.key());
                    if (found != processObjects.end() && itr.value().is_object())
                    {
                        childGeometry = found->second(itr.value(), itr.key());
                    }
                    else if (found != processObjects.end() && !itr.value().is_object())
                    {
                        OSG_WARN << " found a Object JSON node [" << itr.key() <<
                            "] that is not an object or is malformed." << ADD_KEY_NAME << std::endl;
                    }

                    if (!childGeometry || !dynamic_pointer_cast<Geometry>(childGeometry))
                    {
                        OSG_WARN << "WARNING: invalid geometry for MorphTargets." << ADD_KEY_NAME
                            << "[Subkey: " << itr.key()
                            << (itr.value().contains("Name") ? ("[Name: " + itr.value()["Name"].get<std::string>() + "]") : "")
                            << std::endl;
                    }
                    else
                    {
                        morphGeometry->addMorphTarget(dynamic_pointer_cast<Geometry>(childGeometry));
                    }
                }
            }
        }
    }

    // 6) Rig Geometry processing
    if (nodeKey == "osgAnimation.RigGeometry")
    {
        assert(rigGeometry);

        std::map<int, std::string> boneIndexes;

        if (currentJSONNode.contains("SourceGeometry") && currentJSONNode["SourceGeometry"].is_object())
        {
            const json& sourceGeometry = currentJSONNode["SourceGeometry"];

            ref_ptr<Object> childGeometry;
            std::string subKey = "osg.Unknown";
            if (sourceGeometry.contains("osg.Geometry") && sourceGeometry["osg.Geometry"].is_object())
                subKey = "osg.Geometry";
            else if (sourceGeometry.contains("osgAnimation.MorphGeometry") && sourceGeometry["osgAnimation.MorphGeometry"].is_object())
                subKey = "osgAnimation.MorphGeometry";
            childGeometry = parseOsgGeometry(sourceGeometry[subKey], subKey);

            if (!childGeometry || !dynamic_pointer_cast<Geometry>(childGeometry))
            {
                OSG_WARN << "WARNING: invalid geometry for SourceGeometry." << ADD_KEY_NAME
                    << "[Subkey: " << subKey
                    << (sourceGeometry[subKey].contains("Name") ? ("[Name: " + sourceGeometry[subKey]["Name"].get<std::string>() + "]") : "")
                    << std::endl;
            }
            else
            {
                rigGeometry->setSourceGeometry(dynamic_pointer_cast<Geometry>(childGeometry));
                rigGeometry->copyFrom(*dynamic_pointer_cast<Geometry>(childGeometry));

                if (rigGeometry->getName().empty())
                    rigGeometry->setName(childGeometry->getName());
            }
        }

        if (bones && currentJSONNode.contains("BoneMap") && currentJSONNode["BoneMap"].is_object())
        {
            const json& boneMap = currentJSONNode["BoneMap"];

            for (auto& element : boneMap.items())
                boneIndexes[element.value().get<int>()] = element.key();
        }

        if (bones)
        {
            bones->setUserValue("bones", true);
            rigGeometry->setVertexAttribArray(newGeometry->getVertexAttribArrayList().size(), bones);
        }

        if (weights)
        {
            weights->setUserValue("weights", true);
            rigGeometry->setVertexAttribArray(newGeometry->getVertexAttribArrayList().size(), weights);
        }

        // Build influence map
        ParserHelper::makeInfluenceMap(rigGeometry, bones, weights, boneIndexes);

        // Set type of data variance and display lists
        rigGeometry->setDataVariance(osg::Object::DYNAMIC);
        rigGeometry->setUseDisplayList(false);
    }

    // 7) Get external materials (from materialInfo.txt)
    parseExternalMaterials(newGeometry);

    // 8) Vertices post-processing
    if (nodeKey == "osg.Geometry" || nodeKey == "osgAnimation.MorphGeometry")
        postProcessGeometry(newGeometry, currentJSONNode);

    // 9) Done
    return newGeometry;
}

ref_ptr<Object> OsgjsParser::parseComputeBoundingBoxCallback(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    // [Maybe the export is incomplete ? see WriteVisitor::createJSONGeometry]

    return nullptr;
}



ref_ptr<Object> OsgjsParser::parseOsgMaterial(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";

    ref_ptr<Material> newMaterial = new Material;
    Vec4 ambient, diffuse, specular, emission;
    float shininess;

    newMaterial->setName(name);
    newMaterial->setUserValue("UniqueID", currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : -1);

    if (currentJSONNode.contains("Ambient") && currentJSONNode["Ambient"].is_array())
    {
        ambient.set(currentJSONNode["Ambient"][0].get<float>(), currentJSONNode["Ambient"][1].get<float>(),
            currentJSONNode["Ambient"][2].get<float>(), currentJSONNode["Ambient"][3].get<float>());
        newMaterial->setAmbient(Material::FRONT, ambient);
    }
    if (currentJSONNode.contains("Diffuse") && currentJSONNode["Diffuse"].is_array())
    {
        diffuse.set(currentJSONNode["Diffuse"][0].get<float>(), currentJSONNode["Diffuse"][1].get<float>(),
            currentJSONNode["Diffuse"][2].get<float>(), currentJSONNode["Diffuse"][3].get<float>());
        newMaterial->setDiffuse(Material::FRONT, diffuse);
    }
    if (currentJSONNode.contains("Emission") && currentJSONNode["Emission"].is_array())
    {
        emission.set(currentJSONNode["Emission"][0].get<float>(), currentJSONNode["Emission"][1].get<float>(),
            currentJSONNode["Emission"][2].get<float>(), currentJSONNode["Emission"][3].get<float>());
        newMaterial->setEmission(Material::FRONT, emission);
    }
    if (currentJSONNode.contains("Specular") && currentJSONNode["Specular"].is_array())
    {
        specular.set(currentJSONNode["Specular"][0].get<float>(), currentJSONNode["Specular"][1].get<float>(),
            currentJSONNode["Specular"][2].get<float>(), currentJSONNode["Specular"][3].get<float>());
        newMaterial->setSpecular(Material::FRONT, specular);
    }
    if (currentJSONNode.contains("Shininess"))
    {
        shininess = currentJSONNode["Shininess"].get<float>();
        if (shininess < 0) shininess = 0;
        newMaterial->setShininess(Material::FRONT, shininess);
    }

    return newMaterial;
}

ref_ptr<Object> OsgjsParser::parseOsgTexture(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";


    if (!name.empty())
    {
        // Try original file name, then changed to .png
        if (_textureMap.find(name) != _textureMap.end())
            return _textureMap[name];
    }

    std::string fileName = currentJSONNode.contains("File") ? osgDB::getSimpleFileName(currentJSONNode["File"].get<std::string>()) : "";
    ref_ptr<Image> image = getOrCreateImage(fileName);

    if (!image)
        return nullptr;

    ref_ptr<Texture2D> newTexture = new Texture2D;
    newTexture->setName(name);
    newTexture->setImage(image);

    if (currentJSONNode.contains("MagFilter"))
    {
        newTexture->setFilter(Texture::MAG_FILTER, ParserHelper::getFilterModeFromString(currentJSONNode["MagFilter"].get<std::string>()));
    }
    if (currentJSONNode.contains("MinFilter"))
    {
        newTexture->setFilter(Texture::MIN_FILTER, ParserHelper::getFilterModeFromString(currentJSONNode["MinFilter"].get<std::string>()));
    }
    if (currentJSONNode.contains("WrapS"))
    {
        newTexture->setWrap(Texture::WRAP_S, ParserHelper::getWrapModeFromString(currentJSONNode["WrapS"].get<std::string>()));
    }
    if (currentJSONNode.contains("WrapT"))
    {
        newTexture->setWrap(Texture::WRAP_T, ParserHelper::getWrapModeFromString(currentJSONNode["WrapT"].get<std::string>()));
    }

    if (!name.empty())
        _textureMap[name] = newTexture;

    return newTexture;
}

ref_ptr<Object> OsgjsParser::parseOsgBlendFunc(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<BlendFunc> newBlend = new BlendFunc;

    if (currentJSONNode.contains("SourceRGB"))
        newBlend->setSource(ParserHelper::getBlendFuncFromString(currentJSONNode["SourceRGB"]));
    if (currentJSONNode.contains("DestinationRGB"))
        newBlend->setDestination(ParserHelper::getBlendFuncFromString(currentJSONNode["DestinationRGB"]));
    if (currentJSONNode.contains("SourceAlpha"))
        newBlend->setSourceAlpha(ParserHelper::getBlendFuncFromString(currentJSONNode["SourceAlpha"]));
    if (currentJSONNode.contains("DestinationAlpha"))
        newBlend->setDestinationAlpha(ParserHelper::getBlendFuncFromString(currentJSONNode["DestinationAlpha"]));

    return newBlend;
}

ref_ptr<Object> OsgjsParser::parseOsgBlendColor(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<BlendColor> newBlend = new BlendColor;

    if (currentJSONNode.contains("ConstantColor") && currentJSONNode["ConstantColor"].is_array())
    {
        Vec4 colorVec(currentJSONNode["ConstantColor"][0].get<double>(), currentJSONNode["ConstantColor"][1].get<double>(),
            currentJSONNode["ConstantColor"][2].get<double>(), currentJSONNode["ConstantColor"][3].get<double>());

        newBlend->setConstantColor(colorVec);
    }

    return newBlend;
}

ref_ptr<Object> OsgjsParser::parseOsgCullFace(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    if (currentJSONNode.contains("Mode") && currentJSONNode["Mode"].get<std::string>() == "DISABLE")
        return nullptr;

    ref_ptr<CullFace> newCullFace = new CullFace;

    if (currentJSONNode.contains("Mode"))
        newCullFace->setMode(ParserHelper::getCullFaceModeFromString(currentJSONNode["Mode"].get<std::string>()));

    return nullptr;
}




ref_ptr<Object> OsgjsParser::parseOsgTextText(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    OSG_WARN << "WARNING: Scene contains TEXT and this plugin don't support it. Skipping..." << std::endl;

    ref_ptr<Node> dummy = new Node;
    return dummy;
}

ref_ptr<Object> OsgjsParser::parseOsgProjection(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    OSG_WARN << "WARNING: Scene contains PROJECTIONS and this plugin don't support it. Skipping..." << std::endl;

    ref_ptr<Node> dummy = new Node;
    return dummy;
}

ref_ptr<Object> OsgjsParser::parseOsgLight(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    OSG_WARN << "WARNING: Scene contains LIGHTS and this plugin don't export lights. Skipping..." << std::endl;

    ref_ptr<Node> dummy = new Node;
    return dummy;
}

ref_ptr<Object> OsgjsParser::parseOsgLightSource(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    OSG_WARN << "WARNING: Scene contains LIGHT SOURCE and this plugin don't export light sources. Skipping..." << std::endl;

    ref_ptr<Node> dummy = new Node;
    return dummy;
}

ref_ptr<Object> OsgjsParser::parseOsgPagedLOD(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    OSG_WARN << "WARNING: Scene contains PAGE LoD's and this plugin don't export LoD's. Skipping..." << std::endl;

    ref_ptr<Node> dummy = new Node;
    return dummy;
}




ref_ptr<Callback> OsgjsParser::parseOsgAnimationBasicAnimationManager(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<BasicAnimationManager> bam = new BasicAnimationManager;
    bam->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");

    if (_ignoreAnimations)
        return bam;


    if (currentJSONNode.contains("Animations") && currentJSONNode["Animations"].is_array())
    {
        for (const auto& child : currentJSONNode["Animations"])
        {
            // Find subobjects on children nodes - Must be Animation objects.
            for (auto itr = child.begin(); itr != child.end(); ++itr)
            {
                ref_ptr<Object> childAnimation;
                auto found = processObjects.find(itr.key());
                if (found != processObjects.end() && itr.value().is_object())
                {
                    childAnimation = found->second(itr.value(), itr.key());
                }
                else if (found != processObjects.end() && !itr.value().is_object())
                {
                    OSG_WARN << " found a Object JSON node [" << itr.key() <<
                        "] that is not an object or is malformed. " << ADD_KEY_NAME << std::endl;
                }

                if (childAnimation && !dynamic_pointer_cast<Animation>(childAnimation))
                {
                    OSG_WARN << "WARNING: invalid Animation. " << ADD_KEY_NAME
                        << "[Subkey: " << itr.key()
                        << (itr.value().contains("Name") ? ("[Name: " + itr.value()["Name"].get<std::string>() + "]") : "")
                        << std::endl;
                }
                else if (childAnimation)
                {
                    bam->getAnimationList().push_back(dynamic_pointer_cast<Animation>(childAnimation));
                }
            }
        }
    }

    lookForChildren(bam, currentJSONNode, UserDataContainerType::UserData, nodeKey);

    return bam;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationUpdateBone(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<UpdateBone> updateBone = new UpdateBone;
    updateBone->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");

    // Parse other attributes. Currently: osgAnimation.StackedTranslate, osgAnimation.StackedQuaternion, 
    // osgAnimation.StackedRotateAxis, osgAnimation.StackedMatrix, osgAnimation.StackedScale
    if (currentJSONNode.contains("StackedTransforms") && currentJSONNode["StackedTransforms"].is_array())
    {
        for (const auto& child : currentJSONNode["StackedTransforms"])
        {
            // Find subobjects on children nodes - Must be StackedTransform objects.
            for (auto itr = child.begin(); itr != child.end(); ++itr)
            {
                ref_ptr<Object> childTransform;
                auto found = processObjects.find(itr.key());
                if (found != processObjects.end() && itr.value().is_object())
                {
                    childTransform = found->second(itr.value(), itr.key());
                }
                else if (found != processObjects.end() && !itr.value().is_object())
                {
                    OSG_WARN << " found a Object JSON node [" << itr.key() <<
                        "] that is not an object or is malformed. " << ADD_KEY_NAME << std::endl;
                }

                if (childTransform && !dynamic_pointer_cast<StackedTransformElement>(childTransform))
                {
                    OSG_WARN << "WARNING: invalid StackedTransform. " << ADD_KEY_NAME
                        << "[Subkey: " << itr.key()
                        << (itr.value().contains("Name") ? ("[Name: " + itr.value()["Name"].get<std::string>() + "]") : "")
                        << std::endl;
                }
                else if (childTransform)
                {
                    updateBone->getStackedTransforms().push_back(dynamic_pointer_cast<StackedTransformElement>(childTransform));
                }
            }
        }
    }

    lookForChildren(updateBone, currentJSONNode, UserDataContainerType::UserData, nodeKey);

    return updateBone;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationUpdateSkeleton(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return new Callback; // Update Skeleton is a dummy node
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationUpdateMorph(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<UpdateMorph> updateMorph = new UpdateMorph;
    updateMorph->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");

    if (currentJSONNode.contains("TargetMap") && currentJSONNode["TargetMap"].is_object())
    {
        UpdateMorph::TargetNames targets;
        std::map<int, std::string> targetMap; // Needed for sorted map
        for (auto& element : currentJSONNode["TargetMap"].items())
        {
            int key(0);
            if (ParserHelper::getSafeInteger(element.key(), key))
                targetMap[key] = element.value();
        }

        for (auto element = targetMap.begin(); element != targetMap.end(); ++element)
        {
            targets.push_back(element->second);
        }

        updateMorph->setTargetNames(targets);
    }

    lookForChildren(updateMorph, currentJSONNode, UserDataContainerType::UserData, nodeKey);

    return updateMorph;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationUpdateMatrixTransform(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<UpdateMatrixTransform> updateMatrix = new UpdateMatrixTransform;
    updateMatrix->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");

    // Parse other attributes. Currently: osgAnimation.StackedTranslate, osgAnimation.StackedQuaternion, 
    // osgAnimation.StackedRotateAxis, osgAnimation.StackedMatrix, osgAnimation.StackedScale
    if (currentJSONNode.contains("StackedTransforms") && currentJSONNode["StackedTransforms"].is_array())
    {
        for (const auto& child : currentJSONNode["StackedTransforms"])
        {
            // Find subobjects on children nodes - Must be StackedTransform objects.
            for (auto itr = child.begin(); itr != child.end(); ++itr)
            {
                ref_ptr<Object> childTransform;
                auto found = processObjects.find(itr.key());
                if (found != processObjects.end() && itr.value().is_object())
                {
                    childTransform = found->second(itr.value(), itr.key());
                }
                else if (found != processObjects.end() && !itr.value().is_object())
                {
                    OSG_WARN << " found a Object JSON node [" << itr.key() <<
                        "] that is not an object or is malformed. " << ADD_KEY_NAME << std::endl;
                }

                if (childTransform && !dynamic_pointer_cast<StackedTransformElement>(childTransform))
                {
                    OSG_WARN << "WARNING: invalid StackedTransform. " << ADD_KEY_NAME
                        << "[Subkey: " << itr.key()
                        << (itr.value().contains("Name") ? ("[Name: " + itr.value()["Name"].get<std::string>() + "]") : "")
                        << std::endl;
                }
                else if (childTransform)
                {
                    updateMatrix->getStackedTransforms().push_back(dynamic_pointer_cast<StackedTransformElement>(childTransform));
                }
            }
        }
    }

    lookForChildren(updateMatrix, currentJSONNode, UserDataContainerType::UserData, nodeKey);

    return updateMatrix;
}


ref_ptr<Object> OsgjsParser::parseOsgAnimationAnimation(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<Animation> animation = new Animation;
    animation->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");

    if (currentJSONNode.contains("Channels") && currentJSONNode["Channels"].is_array())
    {
        for (const auto& child : currentJSONNode["Channels"])
        {
            // Find subobjects on children nodes - Must be Animation objects.
            for (auto itr = child.begin(); itr != child.end(); ++itr)
            {
                ref_ptr<Object> childChannel;
                auto found = processObjects.find(itr.key());
                if (found != processObjects.end() && itr.value().is_object())
                {
                    childChannel = found->second(itr.value(), itr.key());
                }
                else if (found != processObjects.end() && !itr.value().is_object())
                {
                    OSG_WARN << " found a Object JSON node [" << itr.key() <<
                        "] that is not an object or is malformed. " << ADD_KEY_NAME << std::endl;
                }

                if (childChannel && !dynamic_pointer_cast<Channel>(childChannel))
                {
                    OSG_WARN << "WARNING: invalid Channel. " << ADD_KEY_NAME
                        << "[Subkey: " << itr.key()
                        << (itr.value().contains("Name") ? ("[Name: " + itr.value()["Name"].get<std::string>() + "]") : "")
                        << std::endl;
                }
                else if (childChannel)
                {
                    animation->getChannels().push_back(dynamic_pointer_cast<Channel>(childChannel));
                }
            }
        }
    }

    // Retrieve the first time of series from channels
    double minAnimationTime = std::numeric_limits<double>::max();
    for (auto& channel : animation->getChannels())
    {
        double timeValue(0);
        if (auto vec3Channel = dynamic_cast<osgAnimation::Vec3LinearChannel*>(channel.get()))
        {
            osgAnimation::Vec3KeyframeContainer* keyframes = vec3Channel->getOrCreateSampler()->getOrCreateKeyframeContainer();
            timeValue = keyframes->size() > 0 ? (*keyframes)[0].getTime() : 0;
        }
        else if (auto quatChannel = dynamic_cast<osgAnimation::QuatSphericalLinearChannel*>(channel.get()))
        {
            osgAnimation::QuatKeyframeContainer* keyframes = quatChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();
            timeValue = keyframes->size() > 0 ? (*keyframes)[0].getTime() : 0;
        }
        else if (auto floatChannel = dynamic_cast<osgAnimation::FloatLinearChannel*>(channel.get()))
        {
            osgAnimation::FloatKeyframeContainer* keyframes = floatChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();
            timeValue = keyframes->size() > 0 ? (*keyframes)[0].getTime() : 0;
        }

        if (timeValue < minAnimationTime)
            minAnimationTime = timeValue;
    }

    // Reposition animation timing if necessary.
    if (minAnimationTime > 0 && minAnimationTime != std::numeric_limits<double>::max())
    {
        for (auto& channel : animation->getChannels())
        {
            if (auto vec3Channel = dynamic_cast<osgAnimation::Vec3LinearChannel*>(channel.get()))
            {
                osgAnimation::Vec3KeyframeContainer* keyframes = vec3Channel->getOrCreateSampler()->getOrCreateKeyframeContainer();
                for (osgAnimation::Vec3Keyframe& keyframe : *keyframes)
                {
                    keyframe.setTime(keyframe.getTime() - minAnimationTime);
                }
            }
            else if (auto quatChannel = dynamic_cast<osgAnimation::QuatSphericalLinearChannel*>(channel.get()))
            {
                osgAnimation::QuatKeyframeContainer* keyframes = quatChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();
                for (osgAnimation::QuatKeyframe& keyframe : *keyframes)
                {
                    keyframe.setTime(keyframe.getTime() - minAnimationTime);
                }
            }
            else if (auto floatChannel = dynamic_cast<osgAnimation::FloatLinearChannel*>(channel.get()))
            {
                osgAnimation::FloatKeyframeContainer* keyframes = floatChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();
                for (osgAnimation::FloatKeyframe& keyframe : *keyframes)
                {
                    keyframe.setTime(keyframe.getTime() - minAnimationTime);
                }
            }
        }
    }

    lookForChildren(animation, currentJSONNode, UserDataContainerType::UserData, nodeKey);

    return animation;
}


ref_ptr<Object> OsgjsParser::parseOsgAnimationStackedTranslate(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<StackedTranslateElement> stackedElement = new StackedTranslateElement;
    stackedElement->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");

    if (currentJSONNode.contains("Translate") && currentJSONNode["Translate"].is_array())
    {
        Vec3 vec(currentJSONNode["Translate"][0].get<float>(), currentJSONNode["Translate"][1].get<float>(), currentJSONNode["Translate"][2].get<float>());
        stackedElement->setTranslate(vec);
    }

    return stackedElement;
}

ref_ptr<Object> OsgjsParser::parseOsgAnimationStackedQuaternion(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<StackedQuaternionElement> stackedElement = new StackedQuaternionElement;
    stackedElement->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");

    if (currentJSONNode.contains("Quaternion") && currentJSONNode["Quaternion"].is_array())
    {
        Vec4 vec(currentJSONNode["Quaternion"][0].get<float>(), currentJSONNode["Quaternion"][1].get<float>(), 
            currentJSONNode["Quaternion"][2].get<float>(), currentJSONNode["Quaternion"][3].get<float>());
        stackedElement->setQuaternion(vec);
    }

    return stackedElement;
}

ref_ptr<Object> OsgjsParser::parseOsgAnimationStackedRotateAxis(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<StackedRotateAxisElement> stackedElement = new StackedRotateAxisElement;
    stackedElement->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");

    if (currentJSONNode.contains("Axis") && currentJSONNode["Axis"].is_array())
    {
        Vec3 vec(currentJSONNode["Axis"][0].get<float>(), currentJSONNode["Axis"][1].get<float>(), currentJSONNode["Axis"][2].get<float>());
        stackedElement->setAxis(vec);
    }
    if (currentJSONNode.contains("Angle"))
    {
        stackedElement->setAngle(currentJSONNode["Axis"][1].get<double>());
    }

    return stackedElement;
}

ref_ptr<Object> OsgjsParser::parseOsgAnimationStackedScale(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<StackedScaleElement> stackedElement = new StackedScaleElement;
    stackedElement->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");

    if (currentJSONNode.contains("Scale") && currentJSONNode["Scale"].is_array())
    {
        Vec3 vec(currentJSONNode["Scale"][0].get<float>(), currentJSONNode["Scale"][1].get<float>(), currentJSONNode["Scale"][2].get<float>());
        stackedElement->setScale(vec);
    }

    return stackedElement;
}

ref_ptr<Object> OsgjsParser::parseOsgAnimationStackedMatrix(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<StackedMatrixElement> stackedElement = new StackedMatrixElement;
    stackedElement->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");

    if (currentJSONNode.contains("Matrix") && currentJSONNode["Matrix"].is_array())
    {
        Matrix matrix;
        for (int i = 0; i < 16; i++)
        {
            matrix(i / 4, i % 4) = currentJSONNode["Matrix"][i];
        }
        
        stackedElement->setMatrix(matrix);
    }

    return stackedElement;
}


ref_ptr<Object> OsgjsParser::parseOsgAnimationVec3LerpChannel(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<Vec3LinearChannel> channel = new Vec3LinearChannel;
    channel->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");
    channel->setTargetName(currentJSONNode.contains("TargetName") ? currentJSONNode["TargetName"] : "");

    ref_ptr<Array> keysArray, timesArray;
    uint32_t magic(0); // dummy

    if (currentJSONNode.contains("KeyFrames") && currentJSONNode["KeyFrames"].is_object())
    {
        // Process Time and Key objects
        const json& keyFrames = currentJSONNode["KeyFrames"];

        if (keyFrames.contains("Time") && keyFrames["Time"].is_object())
        {
            const json& time = keyFrames["Time"];
            timesArray = ParserHelper::parseJSONArray(time["Array"], time["ItemSize"], _fileCache, magic);
        }

        if (keyFrames.contains("Key") && keyFrames["Key"].is_object())
        {
            const json& key = keyFrames["Key"];
#ifndef NDEBUG
            UniqueID = key.contains("UniqueID") ? key["UniqueID"].get<int>() : 0;
#endif
            keysArray = ParserHelper::parseJSONArray(key["Array"], key["ItemSize"], _fileCache, magic);
        }

        // Try to decompress arrays
        if (nodeKey == "osgAnimation.Vec3LerpChannelCompressedPacked")
        {
            // Get UserDataContainer early
            lookForChildren(channel, currentJSONNode, UserDataContainerType::UserData, nodeKey);

            // Decode time and keys
            timesArray = ParserHelper::decompressArray(timesArray, channel->getUserDataContainer(), ParserHelper::KeyDecodeMode::TimeCompressed);
            keysArray = ParserHelper::decompressArray(keysArray, channel->getUserDataContainer(), ParserHelper::KeyDecodeMode::Vec3Compressed);
        }

        if ((dynamic_pointer_cast<Vec3Array>(keysArray) || dynamic_pointer_cast<Vec3dArray>(keysArray))
            && dynamic_pointer_cast<FloatArray>(timesArray) || dynamic_pointer_cast<DoubleArray>(timesArray))
        {
            for (unsigned int i = 0; i < timesArray->getNumElements(); ++i)
            {
                Vec3Keyframe f;
                Vec3 vec;

                if (dynamic_pointer_cast<FloatArray>(timesArray))
                    f.setTime((*dynamic_pointer_cast<FloatArray>(timesArray))[i]);
                else
                    f.setTime((*dynamic_pointer_cast<DoubleArray>(timesArray))[i]);
                switch (keysArray->getType())
                {
                case Array::Vec3dArrayType:
                    vec = Vec3((*dynamic_pointer_cast<Vec3dArray>(keysArray))[i].x(),
                        (*dynamic_pointer_cast<Vec3dArray>(keysArray))[i].y(),
                        (*dynamic_pointer_cast<Vec3dArray>(keysArray))[i].z());
                    break;
                case Array::Vec3ArrayType:
                    vec = Vec3((*dynamic_pointer_cast<Vec3Array>(keysArray))[i].x(),
                        (*dynamic_pointer_cast<Vec3Array>(keysArray))[i].y(),
                        (*dynamic_pointer_cast<Vec3Array>(keysArray))[i].z());
                    break;
                }
                f.setValue(vec);
                channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->push_back(f);
            }
        }
    }

    return channel;
}

ref_ptr<Object> OsgjsParser::parseOsgAnimationQuatSlerpChannel(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<QuatSphericalLinearChannel> channel = new QuatSphericalLinearChannel;
    channel->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");
    channel->setTargetName(currentJSONNode.contains("TargetName") ? currentJSONNode["TargetName"] : "");

    ref_ptr<Array> keysArray, timesArray;
    uint32_t magic(0); // dummy

    if (currentJSONNode.contains("KeyFrames") && currentJSONNode["KeyFrames"].is_object())
    {
        // Process Time and Key objects
        const json& keyFrames = currentJSONNode["KeyFrames"];

        if (keyFrames.contains("Time") && keyFrames["Time"].is_object())
        {
            const json& time = keyFrames["Time"];
            timesArray = ParserHelper::parseJSONArray(time["Array"], time["ItemSize"], _fileCache, magic);
        }

        if (keyFrames.contains("Key") && keyFrames["Key"].is_object())
        {
            const json& key = keyFrames["Key"];
            keysArray = ParserHelper::parseJSONArray(key["Array"], key["ItemSize"], _fileCache, magic);
        }

        // Try to decompress arrays
        if (nodeKey == "osgAnimation.QuatSlerpChannelCompressedPacked")
        {
            // Get UserDataContainer early
            lookForChildren(channel, currentJSONNode, UserDataContainerType::UserData, nodeKey);

            // Decode time and keys
            timesArray = ParserHelper::decompressArray(timesArray, channel->getUserDataContainer(), ParserHelper::KeyDecodeMode::TimeCompressed);
            keysArray = ParserHelper::decompressArray(keysArray, channel->getUserDataContainer(), ParserHelper::KeyDecodeMode::QuatCompressed);
        }

        if ((dynamic_pointer_cast<Vec4dArray>(keysArray) || dynamic_pointer_cast<Vec4Array>(keysArray))
            && dynamic_pointer_cast<FloatArray>(timesArray) || dynamic_pointer_cast<DoubleArray>(timesArray))
        {
            for (unsigned int i = 0; i < timesArray->getNumElements(); ++i)
            {
                QuatKeyframe f;
                Quat vec;
                if (dynamic_pointer_cast<FloatArray>(timesArray))
                    f.setTime((*dynamic_pointer_cast<FloatArray>(timesArray))[i]);
                else
                    f.setTime((*dynamic_pointer_cast<DoubleArray>(timesArray))[i]);
                switch (keysArray->getType())
                {
                case Array::Vec4dArrayType:
                    vec = Quat((*dynamic_pointer_cast<Vec4dArray>(keysArray))[i].x(),
                        (*dynamic_pointer_cast<Vec4dArray>(keysArray))[i].y(),
                        (*dynamic_pointer_cast<Vec4dArray>(keysArray))[i].z(),
                        (*dynamic_pointer_cast<Vec4dArray>(keysArray))[i].w());
                    break;
                case Array::Vec4ArrayType:
                    vec = Quat((*dynamic_pointer_cast<Vec4Array>(keysArray))[i].x(),
                        (*dynamic_pointer_cast<Vec4Array>(keysArray))[i].y(),
                        (*dynamic_pointer_cast<Vec4Array>(keysArray))[i].z(),
                        (*dynamic_pointer_cast<Vec4Array>(keysArray))[i].w());
                    break;
                }
                f.setValue(vec);
                channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->push_back(f);
            }
        }
    }

    return channel;
}

ref_ptr<Object> OsgjsParser::parseOsgAnimationFloatLerpChannel(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<FloatLinearChannel> channel = new FloatLinearChannel;
    channel->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");
    channel->setTargetName(currentJSONNode.contains("TargetName") ? currentJSONNode["TargetName"] : "");

    ref_ptr<Array> keysArray, timesArray;
    uint32_t magic(0); // dummy

    if (currentJSONNode.contains("KeyFrames") && currentJSONNode["KeyFrames"].is_object())
    {
        // Process Time and Key objects
        const json& keyFrames = currentJSONNode["KeyFrames"];

        if (keyFrames.contains("Time") && keyFrames["Time"].is_object())
        {
            const json& time = keyFrames["Time"];
            timesArray = ParserHelper::parseJSONArray(time["Array"], time["ItemSize"], _fileCache, magic);
        }

        if (keyFrames.contains("Key") && keyFrames["Key"].is_object())
        {
            const json& key = keyFrames["Key"];
            keysArray = ParserHelper::parseJSONArray(key["Array"], key["ItemSize"], _fileCache, magic);
        }

        if (dynamic_pointer_cast<FloatArray>(keysArray) && dynamic_pointer_cast<FloatArray>(timesArray))
        {
            for (unsigned int i = 0; i < keysArray->getNumElements(); ++i)
            {
                FloatKeyframe f;
                f.setTime((*dynamic_pointer_cast<FloatArray>(timesArray))[i]);
                f.setValue((*dynamic_pointer_cast<FloatArray>(keysArray))[i]);
                channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->push_back(f);
            }
        }
    }

    return channel;
}

ref_ptr<Object> OsgjsParser::parseOsgAnimationFloatCubicBezierChannel(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<FloatCubicBezierChannel> channel = new FloatCubicBezierChannel;
    channel->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");
    channel->setTargetName(currentJSONNode.contains("TargetName") ? currentJSONNode["TargetName"] : "");

    ref_ptr<Array> positionArray, timesArray, controlPointInArray, controlPointOutArray;
    uint32_t magic(0); // dummy

    if (currentJSONNode.contains("KeyFrames") && currentJSONNode["KeyFrames"].is_object())
    {
        // Process Key and Time objects
        const json& keyFrames = currentJSONNode["KeyFrames"];

        if (keyFrames.contains("Time") && keyFrames["Time"].is_object())
        {
            const json& time = keyFrames["Time"];
            timesArray = ParserHelper::parseJSONArray(time["Array"], time["ItemSize"], _fileCache, magic);
        }

        if (keyFrames.contains("Position") && keyFrames["Position"].is_object())
        {
            const json& key = keyFrames["Position"];
            positionArray = ParserHelper::parseJSONArray(key["Array"], key["ItemSize"], _fileCache, magic);
        }

        if (keyFrames.contains("ControlPointIn") && keyFrames["ControlPointIn"].is_object())
        {
            const json& cpoint = keyFrames["ControlPointIn"];
            controlPointInArray = ParserHelper::parseJSONArray(cpoint["Array"], cpoint["ItemSize"], _fileCache, magic);
        }

        if (keyFrames.contains("ControlPointOut") && keyFrames["ControlPointOut"].is_object())
        {
            const json& cpoint = keyFrames["ControlPointOut"];
            controlPointOutArray = ParserHelper::parseJSONArray(cpoint["Array"], cpoint["ItemSize"], _fileCache,magic);
        }


        if (dynamic_pointer_cast<FloatArray>(positionArray) && dynamic_pointer_cast<FloatArray>(timesArray)
            && dynamic_pointer_cast<FloatArray>(controlPointInArray) && dynamic_pointer_cast<FloatArray>(controlPointOutArray))
        {
            for (unsigned int i = 0; i < timesArray->getNumElements(); ++i)
            {
                FloatCubicBezierKeyframe f;
                f.setTime((*dynamic_pointer_cast<FloatArray>(timesArray))[i]);
                f.setValue(FloatCubicBezier((*dynamic_pointer_cast<FloatArray>(positionArray))[i], (*dynamic_pointer_cast<FloatArray>(controlPointInArray))[i],
                    (*dynamic_pointer_cast<FloatArray>(controlPointOutArray))[i]));
                channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->push_back(f);
            }
        }
    }

    return channel;
}

ref_ptr<Object> OsgjsParser::parseOsgAnimationVec3CubicBezierChannel(const json& currentJSONNode, const std::string& nodeKey)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<Vec3CubicBezierChannel> channel = new Vec3CubicBezierChannel;
    channel->setName(currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "");
    channel->setTargetName(currentJSONNode.contains("TargetName") ? currentJSONNode["TargetName"] : "");

    ref_ptr<Array> timesArray, 
        positionArrayX, 
        positionArrayY, 
        positionArrayZ,
        controlPointInArrayX,
        controlPointInArrayY, 
        controlPointInArrayZ, 
        controlPointOutArrayX,
        controlPointOutArrayY,
        controlPointOutArrayZ;

    uint32_t magic(0); // dummy

    if (currentJSONNode.contains("KeyFrames") && currentJSONNode["KeyFrames"].is_object())
    {
        // Process Time and Position objects
        const json& keyFrames = currentJSONNode["KeyFrames"];

        if (keyFrames.contains("Time") && keyFrames["Time"].is_object())
        {
            const json& time = keyFrames["Time"];
            timesArray = ParserHelper::parseJSONArray(time["Array"], time["ItemSize"], _fileCache, magic);
        }

        if (keyFrames.contains("Position") && keyFrames["Position"].is_object())
        {
            const json& key = keyFrames["Position"];
            positionArrayX = ParserHelper::parseJSONArray(key[0]["Array"], key[0]["ItemSize"], _fileCache, magic);
            positionArrayY = ParserHelper::parseJSONArray(key[1]["Array"], key[1]["ItemSize"], _fileCache, magic);
            positionArrayZ = ParserHelper::parseJSONArray(key[2]["Array"], key[2]["ItemSize"], _fileCache, magic);
        }

        if (keyFrames.contains("ControlPointIn") && keyFrames["ControlPointIn"].is_object())
        {
            const json& cpoint = keyFrames["ControlPointIn"];
            controlPointInArrayX = ParserHelper::parseJSONArray(cpoint[0]["Array"], cpoint[0]["ItemSize"], _fileCache, magic);
            controlPointInArrayY = ParserHelper::parseJSONArray(cpoint[1]["Array"], cpoint[1]["ItemSize"], _fileCache, magic);
            controlPointInArrayZ = ParserHelper::parseJSONArray(cpoint[2]["Array"], cpoint[2]["ItemSize"], _fileCache, magic);
        }

        if (keyFrames.contains("ControlPointOut") && keyFrames["ControlPointOut"].is_object())
        {
            const json& cpoint = keyFrames["ControlPointOut"];
            controlPointOutArrayX = ParserHelper::parseJSONArray(cpoint[0]["Array"], cpoint[0]["ItemSize"], _fileCache, magic);
            controlPointOutArrayY = ParserHelper::parseJSONArray(cpoint[1]["Array"], cpoint[1]["ItemSize"], _fileCache, magic);
            controlPointOutArrayZ = ParserHelper::parseJSONArray(cpoint[2]["Array"], cpoint[2]["ItemSize"], _fileCache, magic);
        }

        if (dynamic_pointer_cast<FloatArray>(timesArray) 
            && dynamic_pointer_cast<FloatArray>(controlPointInArrayX) && dynamic_pointer_cast<FloatArray>(controlPointInArrayY) && dynamic_pointer_cast<FloatArray>(controlPointInArrayZ)
            && dynamic_pointer_cast<FloatArray>(controlPointOutArrayX) && dynamic_pointer_cast<FloatArray>(controlPointOutArrayY) && dynamic_pointer_cast<FloatArray>(controlPointOutArrayZ))
        {
            for (unsigned int i = 0; i < timesArray->getNumElements(); ++i)
            {
                Vec3CubicBezierKeyframe f;
                Vec3CubicBezier vec;
                vec.setPosition(Vec3((*dynamic_pointer_cast<FloatArray>(positionArrayX))[i], 
                    (*dynamic_pointer_cast<FloatArray>(positionArrayY))[i], 
                    (*dynamic_pointer_cast<FloatArray>(positionArrayZ))[i]));
                vec.setControlPointIn(Vec3((*dynamic_pointer_cast<FloatArray>(controlPointInArrayX))[i],
                    (*dynamic_pointer_cast<FloatArray>(controlPointInArrayY))[i],
                    (*dynamic_pointer_cast<FloatArray>(controlPointInArrayZ))[i]));
                vec.setControlPointOut(Vec3((*dynamic_pointer_cast<FloatArray>(controlPointOutArrayX))[i],
                    (*dynamic_pointer_cast<FloatArray>(controlPointOutArrayY))[i],
                    (*dynamic_pointer_cast<FloatArray>(controlPointOutArrayZ))[i]));

                f.setTime((*dynamic_pointer_cast<FloatArray>(timesArray))[i]);
                f.setValue(vec);
                channel->getOrCreateSampler()->getOrCreateKeyframeContainer()->push_back(f);
            }
        }
    }

    return channel;
}



std::string OsgjsParser::getModelName() const
{
    std::string fileName = osgDB::findDataFile(MODELINFO_FILE);
    if (fileName.empty())
        return "";

    osgDB::ifstream fin(fileName.c_str());
    json doc;

    std::string modelName;
    if (fin.is_open())
    {
        fin >> doc;

        modelName = doc["name"].get<std::string>();
        OSG_ALWAYS << "INFO: Found model_info.json. Model name is \"" << modelName << "\"" << std::endl;
    }

    return modelName;
}

void OsgjsParser::createTextureMap(const std::map<std::string, TextureInfo2>& textureMap)
{
    for (auto& textureName : textureMap)
    {
        std::string filename = textureName.first;
        std::string realFileName;
        ref_ptr<Image> textureImage;

        // First, search image as original name. If not found, change it to .png
        if (!_fileCache.fileExistsInDirs(filename, realFileName))
        {
            filename = FileCache::stripAllExtensions(filename) + std::string(".png");
        }

        // Then, load texture (cleanup sketchfab names)
        if (_fileCache.fileExistsInDirs(filename, realFileName))
        {
            std::string origExt = osgDB::getLowerCaseFileExtension(realFileName);
            std::string fileNameChanged = FileCache::stripAllExtensions(realFileName) + std::string(".") + origExt;
            std::string textureDir = osgDB::getFilePath(realFileName);
            if (!textureDir.empty())
                textureDir.push_back('\\');

            if (realFileName != (textureDir + fileNameChanged) && std::rename(realFileName.c_str(), (textureDir + fileNameChanged).c_str()) == 0)
            {
                OSG_NOTICE << "INFO: Texture " << osgDB::getSimpleFileName(realFileName) << " renamed to " << fileNameChanged << std::endl;
                realFileName = fileNameChanged;
                _meshMaterials2.renameTexture(textureName.first, osgDB::getSimpleFileName(realFileName));
            }
            else
                _meshMaterials2.renameTexture(textureName.first, osgDB::getSimpleFileName(realFileName));

            textureImage = getOrCreateImage(realFileName);
        }
        else
            OSG_WARN << "WARNING: Missing texture file " << textureName.first << std::endl;
    }
}

ref_ptr<Image> OsgjsParser::getOrCreateImage(const std::string& fileName)
{
    osg::ref_ptr<osg::Image> image;

    // Look for image on map so we don't load the same file twice.
    if (_imageMap.find(fileName) != _imageMap.end())
        return _imageMap.at(fileName);

    std::string fileNameOrig = fileName;
    std::string fileNameChanged = fileName;
    std::string realOrigFileName;

    // Search in dirs
    if (!_fileCache.fileExistsInDirs(fileNameOrig, realOrigFileName))
        return nullptr;

    // First try to read original file name. If unsuccessfull, then retry as .png
    // (need to temporarily rename file. If successful, rename becomes permanent because of later export)
    fileNameChanged = realOrigFileName;
    image = osgDB::readImageFile(realOrigFileName);

    if (!image)
    {
        std::string fileExt = osgDB::getLowerCaseFileExtension(realOrigFileName);

        if (fileExt == "png")
        {
            OSG_WARN << "Unsuported texture format: " << realOrigFileName << std::endl;
            return nullptr;
        }

        else // if (fileExt == "tga" || fileExt == "tiff" || fileExt == "jpg" || fileExt == "jpeg")
        {
            fileNameChanged = FileCache::stripAllExtensions(realOrigFileName) + std::string(".png");
            std::string textureDir = osgDB::getFilePath(realOrigFileName);
            if (!textureDir.empty())
                textureDir.push_back('\\');

            if (std::rename(realOrigFileName.c_str(), (textureDir + fileNameChanged).c_str()) != 0)
            {
                OSG_WARN << "Could not process file: " << realOrigFileName << std::endl;
                return nullptr;
            }

            if (osgDB::fileExists(textureDir + fileNameChanged))
            {
                image = osgDB::readImageFile(textureDir + fileNameChanged);
            }

            if (!image)
            {
                OSG_WARN << "Unsuported texture format: " << realOrigFileName << std::endl;
                std::ignore = std::rename((textureDir + fileNameChanged).c_str(), realOrigFileName.c_str());
                return nullptr;
            }
            else
            {
                OSG_NOTICE << "INFO: " << osgDB::getSimpleFileName(realOrigFileName) << " renamed to " << osgDB::getSimpleFileName(fileNameChanged) << std::endl;
                _meshMaterials2.renameTexture(osgDB::getSimpleFileName(fileNameOrig), osgDB::getSimpleFileName(fileNameChanged));
            }
        }
    }

    fileNameChanged = osgDB::getSimpleFileName(fileNameChanged);
    _imageMap[fileNameChanged] = image;
    return image;
}

void OsgjsParser::CascadeMaterials(osg::Node* node, const std::string& rootMaterialName)
{
    if (auto pGeometry = dynamic_cast<Geometry*>(node))
    {
        ref_ptr<Geometry> geometry = pGeometry;
        parseExternalMaterials(geometry, rootMaterialName);
    }
    else if (auto pGroup = dynamic_cast<Group*>(node))
    {
        for (unsigned int i = 0; i < pGroup->getNumChildren(); ++i)
        {
            CascadeMaterials(pGroup->getChild(i), rootMaterialName);
        }
    }
}

void OsgjsParser::parseExternalMaterials(const ref_ptr<Geometry>& geometry, const std::string& materialNameOverride)
{
    // Only process materials that have external references
    std::string materialName = geometry->getName();
#ifndef NDEBUG
    std::string materialNameDebug = materialName;
#endif
    // Override name (for geodes applying materials to children meshes)
    if (!materialNameOverride.empty())
        materialName = materialNameOverride;

    auto& knownMaterials = _meshMaterials2.getMaterials();

    if (knownMaterials.find(materialName) == knownMaterials.end())
        return;

    auto& knownMaterial = knownMaterials.at(materialName);

    // And only process uncreated materials 
    osg::StateSet* meshState = geometry->getOrCreateStateSet();
    const osg::Material* mat = dynamic_cast<const osg::Material*>(meshState->getAttribute(osg::StateAttribute::MATERIAL));
    if (mat)
        return;

    ref_ptr<Material> newMaterial = new Material;
    newMaterial->setName(materialName);
    meshState->setAttribute(newMaterial, StateAttribute::MATERIAL);

    // Pick missing textures for material
    postProcessStateSet(meshState);

    // Read all channels from KnownMaterial
    for (auto& channel : knownMaterial.Channels)
    {
        std::string channelName = channel.first;
        ChannelInfo2 channelInfo = channel.second;

        if (!channelInfo.Enable)
            continue;

        Vec4 color;
        double factor(0);

        if (channelInfo.Color.size() == 3)
            color = Vec4(channelInfo.Color[0], channelInfo.Color[1], channelInfo.Color[2], 1);
        factor = channelInfo.Factor;

        // Big IF for known channels. Notice one channel may affect multiple Phong surfaces
        if (channelName == "AOPBR" || channelName == "CavityPBR")
            newMaterial->setAmbient(Material::FRONT, color);
        if (channelName == "AlbedoPBR" || channelName == "DiffusePBR" || channelName == "DiffuseColor" 
            || channelName == "CavityPBR" || channelName == "DiffuseIntensity")
            newMaterial->setDiffuse(Material::FRONT, color);
        if (channelName == "Sheen" || channelName == "ClearCoat" || channelName == "SpecularF0" || channelName == "SpecularPBR" ||
            channelName == "SpecularColor" || channelName == "MetalnessPBR" || channelName == "SpecularHardness")
            newMaterial->setSpecular(Material::FRONT, color);
        if (channelName == "Opacity" || channelName == "AlphaMask")
            newMaterial->setTransparency(Material::FRONT, static_cast<float>(factor));
        if (channelName == "EmitColor")
            newMaterial->setEmission(Material::FRONT, color);
        if (channelName == "GlossinessPBR" || channelName == "RoughnessPBR" || channelName == "SheenRoughness")
            newMaterial->setShininess(Material::FRONT, factor);
        // if (channelName == "BumpMap" || channelName == "NormalMap" || channelName == "ClearCoatNormalMap")
    }
}

void OsgjsParser::postProcessGeometry(const ref_ptr<Geometry>& geometry, const json& currentJSONNode, const ref_ptr<Array>& indices)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
    std::string geometryName = geometry->getName();
    int texCoordNum = geometry->getTexCoordArrayList().size();
    texCoordNum = texCoordNum;
#endif // DEBUG

    ref_ptr<Array> verticesOriginals = geometry->getVertexArray();
    ref_ptr<Array> texCoordOriginals = geometry->getTexCoordArray(0);

    // Check for user data
    ref_ptr<osgSim::ShapeAttributeList> shapeAttrList = dynamic_cast<osgSim::ShapeAttributeList*>(geometry->getUserData());
    if (!shapeAttrList)
        return;

    // Get Vertex Shape Attributes
    std::vector<double> vtx_bbl(3, 0);
    std::vector<double> vtx_h(3, 0);
    std::vector<bool> success(12, false);
    double epsilon(0.0), nphi(0.0);

    // Get UV's Shape Attributes
    std::vector<double> uv_bbl(2, 0);
    std::vector<double> uv_h(2, 0);

    success[0] = ParserHelper::getShapeAttribute(shapeAttrList, "vtx_bbl_x", vtx_bbl[0]);
    success[1] = ParserHelper::getShapeAttribute(shapeAttrList, "vtx_bbl_y", vtx_bbl[1]);
    success[2] = ParserHelper::getShapeAttribute(shapeAttrList, "vtx_bbl_z", vtx_bbl[2]);
    success[3] = ParserHelper::getShapeAttribute(shapeAttrList, "vtx_h_x", vtx_h[0]);
    success[4] = ParserHelper::getShapeAttribute(shapeAttrList, "vtx_h_y", vtx_h[1]);
    success[5] = ParserHelper::getShapeAttribute(shapeAttrList, "vtx_h_z", vtx_h[2]);

    ref_ptr<Array> realIndices;

    if (!indices)
    {
        osg::PrimitiveSet* firstPrimitive;
        if (geometry->getPrimitiveSetList().size() == 0)
        {
            return;
        }

        firstPrimitive = geometry->getPrimitiveSet(0);

        // Convert primitive sets into indices array.
        DrawElementsUInt* dei = dynamic_cast<DrawElementsUInt*>(firstPrimitive);
        DrawElementsUShort* des = dynamic_cast<DrawElementsUShort*>(firstPrimitive);
        DrawElementsUByte* deb = dynamic_cast<DrawElementsUByte*>(firstPrimitive);

        if (dei)
            realIndices = new UIntArray(dei->begin(), dei->end());
        else if (des)
            realIndices = new UShortArray(des->begin(), des->end());
        else if (deb)
            realIndices = new UByteArray(deb->begin(), deb->end());
    }
    else
        realIndices = indices;

    if (verticesOriginals && success[0] && success[3])
    {
        if (!realIndices)
        {
            OSG_DEBUG << "WARNING: Encoded Vertices array contains unsupported DrawPrimitive type." << std::endl;
            return;
        }

        ref_ptr<Array> verticesConverted = ParserHelper::decodeVertices(realIndices, verticesOriginals, vtx_bbl, vtx_h);

        if (!verticesConverted)
        {
            OSG_FATAL << "FATAL: Failed to decode vertex array!" << std::endl;
            throw ("Exiting");
        }

        geometry->setVertexArray(verticesConverted);
    }
    
    for (int i = 0; i < 32; i++)
    {
        ref_ptr<Array> texCoord = geometry->getTexCoordArray(i);
        if (!texCoord)
            continue;

        std::stringstream uvbblx, uvbbly, uvhx, uvhy;
        uvbblx << "uv_" << i << "_bbl_x";
        uvbbly << "uv_" << i << "_bbl_y";
        uvhx << "uv_" << i << "_h_x";
        uvhy << "uv_" << i << "_h_y";

        success[6] = ParserHelper::getShapeAttribute(shapeAttrList, uvbblx.str(), uv_bbl[0]);
        success[7] = ParserHelper::getShapeAttribute(shapeAttrList, uvbbly.str(), uv_bbl[1]);
        success[8] = ParserHelper::getShapeAttribute(shapeAttrList, uvhx.str(), uv_h[0]);
        success[9] = ParserHelper::getShapeAttribute(shapeAttrList, uvhy.str(), uv_h[1]);

        if (success[6] && success[8])
        {
            if (!realIndices)
            {
                OSG_DEBUG << "WARNING: Encoded TextCoord array contains unsupported DrawPrimitive type." << std::endl;
                return;
            }

            ref_ptr<Array> texCoordConverted = ParserHelper::decodeVertices(realIndices, texCoord, uv_bbl, uv_h);

            if (!texCoordConverted)
            {
                OSG_WARN << "WARNING: Failed to decode texCoord array!" << std::endl;
                continue;
            }

            geometry->setTexCoordArray(i, texCoordConverted);
        }

        i++;
    }

    success[10] = ParserHelper::getShapeAttribute(shapeAttrList, "epsilon", epsilon);
    success[11] = ParserHelper::getShapeAttribute(shapeAttrList, "nphi", nphi);

    if (success[10] && success[11])
    {
        ref_ptr<Array> normals = geometry->getNormalArray();
        if (normals && normals->getDataSize() == 2)
        {
            ref_ptr<Array> normalsConverted = ParserHelper::decompressArray(normals, geometry->getUserDataContainer(),
                ParserHelper::KeyDecodeMode::NormalsCompressed);

            geometry->setNormalArray(normalsConverted);
        }

        ref_ptr<Array> tangents;
        int index = 0;
        for (auto& attrib : geometry->getVertexAttribArrayList())
        {
            bool tangent;
            if (attrib->getUserValue("tangent", tangent))
            {
                tangents = attrib;
                break;
            }
            index++;
        }

        if (tangents && tangents->getDataSize() == 2)
        {
            ref_ptr<Array> tangentsConverted = ParserHelper::decompressArray(tangents, geometry->getUserDataContainer(),
                ParserHelper::KeyDecodeMode::TangentsCompressed);
            tangentsConverted->setUserValue("tangent", true);
            geometry->setVertexAttribArray(index, tangentsConverted);
        }
    }

    // Process Morph targets
    if (auto morph = dynamic_pointer_cast<MorphGeometry>(geometry))
    {
        for (auto& morphTarget : morph->getMorphTargetList())
        {
            auto morphGeometry = morphTarget.getGeometry();
            if (!morphGeometry || (morphGeometry->getVertexArray() && morphGeometry->getVertexArray()->getNumElements() == 0))
                continue;

            postProcessGeometry(morphGeometry, currentJSONNode, realIndices);
        }
    }
}

void OsgjsParser::postProcessStateSet(const ref_ptr<StateSet>& stateset, const json* currentJSONNode)
{
#ifndef NDEBUG
    std::string debugCurrentJSONNode = currentJSONNode ? currentJSONNode->dump() : "";
    std::string name = currentJSONNode ? (currentJSONNode->contains("Name") ? (*currentJSONNode)["Name"] : "") : "";
    int UniqueID = currentJSONNode ? (currentJSONNode->contains("UniqueID") ? (*currentJSONNode)["UniqueID"].get<int>() : 0) : -1;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    // Try to get textures for material from stateset and model_info.txt
    osg::Material* material = dynamic_cast<osg::Material*>(stateset->getAttribute(osg::StateAttribute::MATERIAL));

    if (!material)
        return;

    // Fill up supported textures
    std::string materialName = material->getName();
    std::unordered_set<std::string> unfoundTextures;

    auto knownMaterials = _meshMaterials2.getMaterials();
    auto knownMaterial = knownMaterials.find(materialName);
    if (knownMaterial != knownMaterials.end())
    {
        auto& materialEntry = knownMaterial->second;

        // Get all channels' textures.
        for (auto& channel : materialEntry.Channels)
        {
            auto& textureInfo = channel.second.Texture;
            std::string channelName = channel.first;
            if (!channel.second.Enable || textureInfo.Name.empty())
                continue;

            material->setUserValue(std::string("textureLayer_") + channel.first, osgDB::getSimpleFileName(textureInfo.Name));
            unfoundTextures.emplace(textureInfo.Name);
        }
    }
    else
        return;

    // First, search for pre-created textures on StateSet
    for (unsigned int i = 0; i < stateset->getNumTextureAttributeLists(); i++)
    {
        const Texture* tex = dynamic_cast<const Texture*>(stateset->getTextureAttribute(i, osg::StateAttribute::TEXTURE));
        if (tex)
        {
            // Remove found textures from unfound set
            const Image* texImage = tex->getImage(0);
            std::string imageName = texImage->getFileName();
            unfoundTextures.erase(imageName);
        }
    }
    
    // Next, create all missing textures with default parameters
    int j = stateset->getNumTextureAttributeLists();
    for (auto& unfoundTexture : unfoundTextures)
    {
        if (_textureMap.find(unfoundTexture) != _textureMap.end())
        {
            stateset->setTextureAttribute(j++, _textureMap[unfoundTexture], StateAttribute::TEXTURE);
            continue;
        }

        ref_ptr<Image> image = getOrCreateImage(unfoundTexture);

        if (!image)
            continue;

        ref_ptr<Texture2D> texture = new Texture2D;

        texture->setName(unfoundTexture);
        texture->setImage(image);
        texture->setFilter(Texture::MAG_FILTER, Texture::LINEAR);
        texture->setFilter(Texture::MIN_FILTER, Texture::LINEAR);
        texture->setWrap(Texture::WRAP_S, Texture::REPEAT);
        texture->setWrap(Texture::WRAP_T, Texture::REPEAT);

        stateset->setTextureAttribute(j++, texture, StateAttribute::TEXTURE);
        _textureMap[unfoundTexture] = texture;
    }
}





