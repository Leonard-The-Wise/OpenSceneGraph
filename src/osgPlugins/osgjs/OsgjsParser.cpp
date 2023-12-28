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

void OsgjsParser::lookForChildren(ref_ptr<Object> object, const json& currentJSONNode, UserDataContainerType containerType, const std::string& nodeKey)
{
#ifdef DEBUG
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
                notify(WARN) << "WARNING: object " << object->getName() + " had not parseable children. ->" << std::endl << ADD_KEY_NAME << std::endl;
        }
    }

    // Get User Data Containers for object
    if (currentJSONNode.contains("UserDataContainer"))
        parseUserDataContainer(object, currentJSONNode["UserDataContainer"], containerType, nodeKey);

    // Get object state set
    if (currentJSONNode.contains("StateSet"))
        parseStateSet(object, currentJSONNode["StateSet"], nodeKey);

    // Get UpdateCallbacks for animations
    if (currentJSONNode.contains("UpdateCallbacks") && currentJSONNode["UpdateCallbacks"].is_array())
    {
        for (const auto& child : currentJSONNode["UpdateCallbacks"])
        {
            ref_ptr<Callback> newCallback = new Callback;
            if (!parseCallback(newCallback, child, nodeKey) || dynamic_pointer_cast<Node>(object) == false)
            {
                notify(WARN) << "WARNING: Could not apply animation callbacks to object. ->" << std::endl << ADD_KEY_NAME << std::endl;
                break;
            }
            else
                dynamic_pointer_cast<Node>(object)->addUpdateCallback(newCallback);
        }
    }
}

bool OsgjsParser::parseObject(ref_ptr<Object> currentObject, const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
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
                notify(WARN) << " found a Object JSON node [" << itr.key() <<
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
                notify(WARN) << "WARNING: Could not find Drawable geometry in Geode node!" << ADD_KEY_NAME << std::endl;
        }
        else if (dynamic_pointer_cast<Group>(currentObject))
            dynamic_pointer_cast<Group>(currentObject)->addChild(reinterpret_cast<Node*>(newObject.get()));
        else
        {
            notify(FATAL) << "Something went wrong and object tree is broken!" << ADD_KEY_NAME << std::endl;
            return false;
        }

        return true;
    }

    return false;
}

bool OsgjsParser::parseCallback(ref_ptr<Callback> currentCallback, const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    // FIXME: Implement.
    return true;

    if (currentJSONNode.is_object())
    {
        // Create a new node to accomodate the object
        ref_ptr<Callback> newCallback;

        // Lookup current node vertically, searching for JSON Callbacks to process
        for (auto itr = currentJSONNode.begin(); itr != currentJSONNode.end(); ++itr)
        {
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
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    switch (containerType)
    {
    case UserDataContainerType::None:
    {
        notify(WARN) << "Container for current object has no specification!" << ADD_KEY_NAME << std::endl;
        break;
    }
    case UserDataContainerType::UserData:
    {
        ref_ptr<DefaultUserDataContainer> udc = new DefaultUserDataContainer;
        if (currentJSONNode.contains("Name"))
            udc->setName(currentJSONNode["Name"]);

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
        if (currentJSONNode.contains("Name"))
            shapeAttrList->setName(currentJSONNode["Name"]);

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
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

}



ref_ptr<Object> OsgjsParser::parseOsgNode(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

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
                if (geodeNodes.find(itr.key()) != geodeNodes.end())
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
    ref_ptr<Node> newObject;
    if (isGeode)
        newObject = new Geode;
    else
        newObject = new Group;

    // Add name information to node
    if (currentJSONNode.contains("Name"))
        newObject->setName(currentJSONNode["Name"]);

    lookForChildren(newObject, currentJSONNode, isGeode ? UserDataContainerType::ShapeAttributes : UserDataContainerType::UserData, nodeKey);

    return newObject;
}



ref_ptr<Object> OsgjsParser::parseOsgMatrixTransform(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    // Create a matrix transform
    ref_ptr<MatrixTransform> newObject;

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
    if (currentJSONNode.contains("Name"))
        newObject->setName(currentJSONNode["Name"]);

    // Get the matrix
    if (!currentJSONNode.contains("Matrix") || !currentJSONNode["Matrix"].is_array() || currentJSONNode["Matrix"].size() != 16)
    {
        notify(WARN) << "WARNING: MatrixTransform's Matrix object does not exist or have incorrect size!" << ADD_KEY_NAME << std::endl;
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

        // Fix rotate and scale
        if (_firstMatrix)
        {
            matrix.postMult(osg::Matrix::rotate(osg::inDegrees(-90.0), osg::X_AXIS));
            matrix.postMult(osg::Matrix::scale(100.0, 100.0, 100.0));
            _firstMatrix = false;
        }

        if (bone)
            bone->setMatrixInSkeletonSpace(matrix);
        else
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
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    ref_ptr<Geometry> newGeometry;

    // Polymorph based on node key
    if (nodeKey == "osg.Geometry")
        newGeometry = new Geometry;
    else if (nodeKey == "osgAnimation.MorphGeometry")
        newGeometry = new MorphGeometry;
    else if (nodeKey == "osgAnimation.RigGeometry")
        newGeometry = new RigGeometry;
    else
    {
        notify(WARN) << "WARNING: Unknown geometry node!" << ADD_KEY_NAME << std::endl;
        return nullptr;
    }

    // Helper pointers
    RigGeometry* rigGeometry = dynamic_pointer_cast<RigGeometry>(newGeometry);
    MorphGeometry* morphGeometry = dynamic_pointer_cast<MorphGeometry>(newGeometry);

    // Add name information to node
    if (currentJSONNode.contains("Name"))
        newGeometry->setName(currentJSONNode["Name"]);

    ref_ptr<Array> vertexes;
    ref_ptr<Array> normals;
    ref_ptr<Array> colors;
    ref_ptr<Array> tangents;
    ref_ptr<Array> bones;
    ref_ptr<Array> weights;
    std::vector<ref_ptr<Array>> texcoords;
    ref_ptr<Array> indices;
    int vertexAttribArrays = 0;

    // 1) Parse Vertex Attributes List
    if (currentJSONNode.contains("VertexAttributeList") && currentJSONNode["VertexAttributeList"].is_object())
    {
        const json* vertexAttributeList = &currentJSONNode["VertexAttributeList"];
        const json* vertexNode = nullptr;
        const json* normalNode = nullptr;
        const json* colorNode = nullptr;
        const json* tangentNode = nullptr;
        const json* bonesNode = nullptr;
        const json* weightsNode = nullptr;

        std::vector<const json*> texCoordNodes;

        // 1.1) Get VertexAttributeList members

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
                texCoordNodes.push_back(&(*vertexAttributeList)[ss.str()]);
        }

        // 1.2) Get VertexAttributeList arrays
#ifdef DEBUG
        std::string vertexNodestr = vertexNode ? vertexNode->dump() : "";
        std::string normalNodestr = normalNode ? normalNode->dump(): "";
        std::string colorNodestr = colorNode ? colorNode->dump() : "";
        std::string tangentNodestr = tangentNode ? tangentNode->dump() : "";
        std::string bonesNodestr = bonesNode ? bonesNode->dump() : "";
        std::string weightsNodestr = weightsNode ? weightsNode->dump() : "";
#endif

        if (vertexNode && vertexNode->contains("Array") && (*vertexNode)["Array"].is_object() && vertexNode->contains("ItemSize") && (*vertexNode)["ItemSize"].is_number())
            vertexes = ParserHelper::parseJSONArray((*vertexNode)["Array"], (*vertexNode)["ItemSize"].get<int>(), _fileCache);
        if (normalNode && normalNode->contains("Array") && (*normalNode)["Array"].is_object() && normalNode->contains("ItemSize") && (*normalNode)["ItemSize"].is_number())
            normals = ParserHelper::parseJSONArray((*normalNode)["Array"], (*normalNode)["ItemSize"].get<int>(), _fileCache);
        if (colorNode && colorNode->contains("Array") && (*colorNode)["Array"].is_object() && colorNode->contains("ItemSize") && (*colorNode)["ItemSize"].is_number())
            colors = ParserHelper::parseJSONArray((*colorNode)["Array"], (*colorNode)["ItemSize"].get<int>(), _fileCache);
        if (tangentNode && tangentNode->contains("Array") && (*tangentNode)["Array"].is_object() && tangentNode->contains("ItemSize") && (*tangentNode)["ItemSize"].is_number())
            tangents = ParserHelper::parseJSONArray((*tangentNode)["Array"], (*tangentNode)["ItemSize"].get<int>(), _fileCache);
        if (bonesNode && bonesNode->contains("Array") && (*bonesNode)["Array"].is_object() && bonesNode->contains("ItemSize") && (*bonesNode)["ItemSize"].is_number())
            bones = ParserHelper::parseJSONArray((*bonesNode)["Array"], (*bonesNode)["ItemSize"].get<int>(), _fileCache);
        if (weightsNode && weightsNode->contains("Array") && (*weightsNode)["Array"].is_object() && weightsNode->contains("ItemSize") && (*weightsNode)["ItemSize"].is_number())
            weights = ParserHelper::parseJSONArray((*weightsNode)["Array"], (*weightsNode)["ItemSize"].get<int>(), _fileCache);
        for (auto& texCoordNode : texCoordNodes)
            if (texCoordNode->contains("Array") && (*texCoordNode)["Array"].is_object() && texCoordNode->contains("ItemSize") && (*texCoordNode)["ItemSize"].is_number())
                texcoords.push_back(ParserHelper::parseJSONArray((*texCoordNode)["Array"], (*texCoordNode)["ItemSize"].get<int>(), _fileCache));

        // 1.3) Sanity checks
        if (nodeKey == "osg.Geometry")
        {
            if (vertexes)
            {
                if (vertexes->getNumElements() == 0)
                {
                    osg::notify(osg::WARN) << "WARNING: Model contains a geometry node without any vertices. Ignoring..." << ADD_KEY_NAME << std::endl;
                    return newGeometry;
                }
                if (normals && vertexes->getNumElements() != normals->getNumElements())
                {
                    osg::notify(osg::WARN) << "WARNING: Model contains normals that don't match number of vertices..." << ADD_KEY_NAME << std::endl;
                }
                if (tangents && vertexes->getNumElements() != tangents->getNumElements())
                {
                    osg::notify(osg::WARN) << "WARNING: Model contains tangents that don't match number of vertices..." << ADD_KEY_NAME << std::endl;
                }
                if (colors && vertexes->getNumElements() != colors->getNumElements())
                {
                    osg::notify(osg::WARN) << "WARNING: Model contains colors that don't match number of vertices..." << ADD_KEY_NAME << std::endl;
                }
                bool texError = false;
                for (auto& texcoordcheck : texcoords)
                    if (vertexes->getNumElements() != texcoordcheck->getNumElements())
                        texError = true;

                if (texError)
                    osg::notify(osg::WARN) << "WARNING: Model contain 1 or more texCoords that don't match number of vertices..." << ADD_KEY_NAME << std::endl;
            }
        }

        // 1.4) Set Geometry Attributes

        if (vertexes)
            newGeometry->setVertexArray(vertexes);
        if (normals)
            newGeometry->setNormalArray(normals, Array::BIND_PER_VERTEX);
        if (colors)
            newGeometry->setColorArray(colors, Array::BIND_PER_VERTEX);
        if (tangents)
        {
            tangents->setUserValue("tangent", true);
            newGeometry->setVertexAttribArray(vertexAttribArrays++, tangents);
        }
        int i = 0;
        for (auto& texcoord : texcoords)
        {
            newGeometry->setTexCoordArray(i, texcoord);
            ++i;
        }
    }

    // 2) Parse Primitive Set List
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
                notify(WARN) << "WARNING: Unsuported primitive type. Skipping." << std::endl;
                continue;
            }

            if (newPrimitiveSet && newDrawElementNode)
            {
                // Parse Draw modes
                if ((*newDrawElementNode).contains("Mode"))
                {
                    GLenum drawMode = ParserHelper::getModeFromString((*newDrawElementNode)["Mode"].get<std::string>());
                    newPrimitiveSet->setMode(drawMode);
                }

                // Process DrawElement objects
                if ((*newDrawElementNode).contains("Indices") && (*newDrawElementNode)["Indices"].is_object())
                {
                    const json& newPrimitiveIndices = (*newDrawElementNode)["Indices"];
                    if (newPrimitiveIndices.contains("Array") && newPrimitiveIndices["Array"].is_object() && newPrimitiveIndices.contains("ItemSize") && newPrimitiveIndices["ItemSize"].is_number())
                    {
                        indices = ParserHelper::parseJSONArray(newPrimitiveIndices["Array"], 
                            newPrimitiveIndices["ItemSize"].get<int>(), _fileCache);

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

    // 3) Get ComputBoundingBoxCallback
    if (currentJSONNode.contains("osg.ComputeBoundingBoxCallback") && currentJSONNode["osg.ComputeBoundingBoxCallback"].is_object())
    {
        std::ignore = parseComputeBoundingBoxCallback(currentJSONNode["osg.ComputeBoundingBoxCallback"], "osg.ComputeBoundingBoxCallback");
    }

    // 4) Morph Geometry processing
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
                        notify(WARN) << " found a Object JSON node [" << itr.key() <<
                            "] that is not an object or is malformed." << ADD_KEY_NAME << std::endl;
                    }

                    if (!childGeometry || !dynamic_pointer_cast<Geometry>(childGeometry))
                        notify(WARN) << "WARNING: invalid geometry for MorphTargets." << ADD_KEY_NAME
                        << "[Subkey: " << itr.key()
                        << (itr.value().contains("Name") ? ("[Name: " + itr.value()["Name"].get<std::string>() + "]") : "")
                        << std::endl;
                    else
                        morphGeometry->addMorphTarget(dynamic_pointer_cast<Geometry>(childGeometry));

                }
            }
        }
    }

    // 5) Rig Geometry processing
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
                    notify(WARN) << "WARNING: invalid geometry for SourceGeometry." << ADD_KEY_NAME
                        << "[Subkey: " << subKey
                        << (sourceGeometry[subKey].contains("Name") ? ("[Name: " + sourceGeometry[subKey]["Name"].get<std::string>() + "]") : "")
                        << std::endl;
            else
            {
                rigGeometry->setSourceGeometry(dynamic_pointer_cast<Geometry>(childGeometry));
                rigGeometry->copyFrom(*dynamic_pointer_cast<Geometry>(childGeometry));
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
            rigGeometry->setVertexAttribArray(vertexAttribArrays++, bones);
        }

        if (weights)
        {
            bones->setUserValue("weights", true);
            rigGeometry->setVertexAttribArray(vertexAttribArrays++, weights);
        }

        // Build influence map
        ParserHelper::makeInfluenceMap(rigGeometry, bones, weights, boneIndexes);

        // Set type of data variance and display lists
        rigGeometry->setDataVariance(osg::Object::DYNAMIC);
        //rigGeometry->setUseDisplayList(false);
        rigGeometry->setRigTransformImplementation(new osgAnimation::RigTransformSoftware);
    }

    // 6) Get object statesets and userData
    lookForChildren(newGeometry, currentJSONNode, UserDataContainerType::ShapeAttributes, nodeKey);

    // 7) Done
    return newGeometry;
}

ref_ptr<Object> OsgjsParser::parseComputeBoundingBoxCallback(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
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
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Object> OsgjsParser::parseOsgTexture(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Object> OsgjsParser::parseOsgBlendFunc(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Object> OsgjsParser::parseOsgBlendColor(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Object> OsgjsParser::parseOsgCullFace(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}




ref_ptr<Object> OsgjsParser::parseOsgTextText(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Object> OsgjsParser::parseOsgProjection(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Object> OsgjsParser::parseOsgLight(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Object> OsgjsParser::parseOsgLightSource(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Object> OsgjsParser::parseOsgPagedLOD(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}




ref_ptr<Callback> OsgjsParser::parseOsgAnimationBasicAnimationManager(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationAnimation(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}


ref_ptr<Callback> OsgjsParser::parseOsgAnimationUpdateBone(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationUpdateSkeleton(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationUpdateMorph(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationUpdateMatrixTransform(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}


ref_ptr<Callback> OsgjsParser::parseOsgAnimationStackedTranslate(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationStackedQuaternion(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationStackedRotateAxis(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationStackedScale(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationStackedMatrix(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}


ref_ptr<Callback> OsgjsParser::parseOsgAnimationVec3LerpChannel(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationQuatSLerpChannel(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationVec3LerpChannelCompressedPack(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationQuatSLerpChannelCompressedPack(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}


ref_ptr<Callback> OsgjsParser::parseOsgAnimationFloatLerpChannel(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationFloatCubicBezierChannel(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}

ref_ptr<Callback> OsgjsParser::parseOsgAnimationVec3CubicBezierChannel(const json& currentJSONNode, const std::string& nodeKey)
{
#ifdef DEBUG
    std::string debugCurrentJSONNode = currentJSONNode.dump();
    std::string name = currentJSONNode.contains("Name") ? currentJSONNode["Name"] : "";
    int UniqueID = currentJSONNode.contains("UniqueID") ? currentJSONNode["UniqueID"].get<int>() : 0;
    UniqueID = UniqueID; // Bypass compilation warning
#endif

    return nullptr;
}








