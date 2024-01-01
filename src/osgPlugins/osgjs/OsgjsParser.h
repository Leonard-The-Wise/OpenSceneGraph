#pragma once

namespace osgJSONParser
{

    class DebugNode {
    public:
        std::string name; 
        std::vector<std::shared_ptr<DebugNode>> children; 

        explicit DebugNode(const std::string& val) : name(val) {}

        void addChild(const std::shared_ptr<DebugNode>& child) {
            children.push_back(child);
        }
    };

    class DebugTree {
    public:
        std::shared_ptr<DebugNode> root;

        DebugTree() : root(std::make_shared<DebugNode>("Root")) {}

        std::shared_ptr<DebugNode> addNode(const std::string& value, const std::shared_ptr<DebugNode>& parent) {
            auto newNode = std::make_shared<DebugNode>(value);
            parent->addChild(newNode);
            return newNode;
        }

        void printTree() const {
            printSubtree(root, 0);
        }

    private:

        void printSubtree(const std::shared_ptr<DebugNode>& node, int depth) const {
            if (node) {
                std::string indent(depth * 2, ' '); 
                std::cout << indent << node->name << std::endl;
                for (const auto& child : node->children) {
                    printSubtree(child, depth + 1); 
                }
            }
        }
    };

    enum class UserDataContainerType {
        None, UserData, ShapeAttributes
    };

	class OsgjsParser
	{
	public:
        using json = nlohmann::json;

		OsgjsParser():
            processObjects {
                {"osg.Node", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgNode(json, nodeKey); }},

                {"osg.MatrixTransform", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgMatrixTransform(json, nodeKey); }},
                {"osg.Geometry", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgGeometry(json, nodeKey); }},
                {"osgAnimation.RigGeometry", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgGeometry(json, nodeKey); }},
                {"osgAnimation.MorphGeometry", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgGeometry(json, nodeKey); }},
                {"osgAnimation.Skeleton", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgMatrixTransform(json, nodeKey); }},
                {"osgAnimation.Bone", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgMatrixTransform(json, nodeKey); }},
                {"osg.ComputeBoundingBoxCallback", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseComputeBoundingBoxCallback(json, nodeKey); }},

                {"osg.Material", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgMaterial(json, nodeKey); }},
                {"osg.Texture", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgTexture(json, nodeKey); }},
                {"osg.BlendFunc", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgBlendFunc(json, nodeKey); }},
                {"osg.BlendColor", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgBlendColor(json, nodeKey); }},
                {"osg.CullFace", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgCullFace(json, nodeKey); }},

                {"osgText.Text", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgTextText(json, nodeKey); }},
                {"osg.Projection", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgProjection(json, nodeKey); }},
                {"osg.Light", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgLight(json, nodeKey); }},
                {"osg.LightSource", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgLightSource(json, nodeKey); }},
                {"osg.PagedLOD", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Object> {return this->parseOsgPagedLOD(json, nodeKey); }},
            },
            processCallbacks {
                {"osgAnimation.BasicAnimationManager", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationBasicAnimationManager(json, nodeKey); }},
                { "osgAnimation.Animation", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationAnimation(json, nodeKey); }},

                { "osgAnimation.UpdateBone", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationUpdateBone(json, nodeKey); }},
                { "osgAnimation.UpdateSkeleton", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationUpdateSkeleton(json, nodeKey); }},
                { "osgAnimation.UpdateMorph", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationUpdateMorph(json, nodeKey); }},
                { "osgAnimation.UpdateMatrixTransform", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationUpdateMatrixTransform(json, nodeKey); }},

                { "osgAnimation.StackedTranslate", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationStackedTranslate(json, nodeKey); }},
                { "osgAnimation.StackedQuaternion", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationStackedQuaternion(json, nodeKey); }},
                { "osgAnimation.StackedRotateAxis", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationStackedRotateAxis(json, nodeKey); }},
                { "osgAnimation.StackedMatrix", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationStackedMatrix(json, nodeKey); }},
                { "osgAnimation.StackedScale", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationStackedScale(json, nodeKey); }},

                { "osgAnimation.Vec3LerpChannel", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationVec3LerpChannel(json, nodeKey); }},
                { "osgAnimation.QuatSLerpChannel", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationQuatSLerpChannel(json, nodeKey); }},
                { "osgAnimation.Vec3LerpChannelCompressedPack", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationVec3LerpChannelCompressedPack(json, nodeKey); }},
                { "osgAnimation.QuatSLerpChannelCompressedPack", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationQuatSLerpChannelCompressedPack(json, nodeKey); }},

                { "osgAnimation.FloatLerpChannel", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationFloatLerpChannel(json, nodeKey); }},
                { "osgAnimation.FloatCubicBezierChannel", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationFloatCubicBezierChannel(json, nodeKey); }},
                { "osgAnimation.Vec3CubicBezierChannel", [this](const json& json, const std::string& nodeKey) -> osg::ref_ptr<osg::Callback> {return this->parseOsgAnimationVec3CubicBezierChannel(json, nodeKey); }},
            }, 
            drawableNodes{
                "osg.Geometry",
                "osgAnimation.RigGeometry",
                "osgAnimation.MorphGeometry",
                "osgText.Text"
            }
        {};

		inline void setFileCache(const FileCache& fileCache) 
		{ _fileCache = fileCache; };

        inline void setNeedDecodeIndices(bool need)
        {
            _needDecodeIndices = need;
        }

		osg::ref_ptr<osg::Group> parseObjectTree(const json& firstOsgNodeJSON);

	private:

		FileCache _fileCache;
        bool _firstMatrix = true;
        bool _needDecodeIndices = true;

        const std::unordered_map<std::string, std::function<osg::ref_ptr<osg::Object>(const json&, const std::string& nodeKey)>> processObjects;
        const std::unordered_map<std::string, std::function<osg::ref_ptr<osg::Callback>(const json&, const std::string& nodeKey)>> processCallbacks;
        const std::unordered_set<std::string> drawableNodes;

        void lookForChildren(osg::ref_ptr<osg::Object> object, const json& currentJSONNode, UserDataContainerType containerType, const std::string& nodeKey);

        bool parseObject(osg::ref_ptr<osg::Object> currentObject, const json& currentJSONNode, const std::string& nodeKey);
        bool parseCallback(osg::ref_ptr<osg::Callback> currentCallback, const json& currentJSONNode, const std::string& nodeKey);
        void parseUserDataContainer(osg::ref_ptr<osg::Object> currentObject, const json& currentJSONNode, UserDataContainerType containerType, const std::string& nodeKey);
        void parseStateSet(osg::ref_ptr<osg::Object> currentObject, const json& currentJSONNode, const std::string& nodeKey);

        // OSG Objects

        osg::ref_ptr<osg::Object> parseOsgNode(const json& currentJSONNode, const std::string& nodeKey);

        osg::ref_ptr<osg::Object> parseOsgMatrixTransform(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Object> parseOsgGeometry(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Object> parseComputeBoundingBoxCallback(const json& currentJSONNode, const std::string& nodeKey);

        osg::ref_ptr<osg::Object> parseOsgMaterial(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Object> parseOsgTexture(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Object> parseOsgBlendFunc(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Object> parseOsgBlendColor(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Object> parseOsgCullFace(const json& currentJSONNode, const std::string& nodeKey);

        osg::ref_ptr<osg::Object> parseOsgTextText(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Object> parseOsgProjection(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Object> parseOsgLight(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Object> parseOsgLightSource(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Object> parseOsgPagedLOD(const json& currentJSONNode, const std::string& nodeKey);

        // OSG Animations Callbacks

        osg::ref_ptr<osg::Callback> parseOsgAnimationBasicAnimationManager(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationAnimation(const json& currentJSONNode, const std::string& nodeKey);

        osg::ref_ptr<osg::Callback> parseOsgAnimationUpdateBone(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationUpdateSkeleton(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationUpdateMorph(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationUpdateMatrixTransform(const json& currentJSONNode, const std::string& nodeKey);

        osg::ref_ptr<osg::Callback> parseOsgAnimationStackedTranslate(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationStackedQuaternion(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationStackedRotateAxis(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationStackedMatrix(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationStackedScale(const json& currentJSONNode, const std::string& nodeKey);

        osg::ref_ptr<osg::Callback> parseOsgAnimationVec3LerpChannel(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationQuatSLerpChannel(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationVec3LerpChannelCompressedPack(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationQuatSLerpChannelCompressedPack(const json& currentJSONNode, const std::string& nodeKey);

        osg::ref_ptr<osg::Callback> parseOsgAnimationFloatLerpChannel(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationFloatCubicBezierChannel(const json& currentJSONNode, const std::string& nodeKey);
        osg::ref_ptr<osg::Callback> parseOsgAnimationVec3CubicBezierChannel(const json& currentJSONNode, const std::string& nodeKey);

        void postProcessGeometry(osg::ref_ptr<osg::Geometry> geometry);

	};

}