
#include "pch.h"

#include <unordered_map>

#include "tiny_gltf.h"

#include "Stringify.h"
#include "json.hpp"

#include "MaterialParser.h"

//! Visitor that builds a GLTF data model from an OSG scene graph.
class OSGtoGLTF : public osg::NodeVisitor
{

private:
    typedef std::map<osg::ref_ptr<const osg::Node>, int> OsgNodeSequenceMap;
    typedef std::map<osg::ref_ptr<const osg::BufferData>, int> ArraySequenceMap;
    typedef std::map<osg::ref_ptr<const osg::Array>, int> AccessorSequenceMap;
    typedef std::vector< osg::ref_ptr< osg::StateSet > > StateSetStack;
    typedef std::map<int, const osg::Matrix*> BindMatrices;
    typedef std::map<std::string, int> BoneIDNames;
    typedef std::map<int, osg::ref_ptr<osgAnimation::RigGeometry>> RiggedMeshStack;

    std::vector< osg::ref_ptr< osg::Texture > > _textures;

    tinygltf::Model& _model;
    std::stack<tinygltf::Node*> _gltfNodeStack;
    OsgNodeSequenceMap _osgNodeSeqMap;
    ArraySequenceMap _buffers;
    ArraySequenceMap _bufferViews;
    ArraySequenceMap _accessors;
    StateSetStack _ssStack;
    RiggedMeshStack _riggedMeshMap;
    bool _firstMatrix;
    bool _firstNamedMatrix;
    osg::Node* _firstMatrixNode;
    std::string _modelName;

    std::stack<std::pair<int, tinygltf::Skin*>> _gltfSkeletons;
    BindMatrices _skeletonInvBindMatrices;
    BoneIDNames _gltfBoneIDNames;

    std::set<std::string> _animationTargetNames;          // Animation targets (for osg animated matrices)
    std::set<std::string> _discardedAnimationTargetNames; // We discard animation targets with 1 keyframe and mark them so we don't get unecessary warnings about missing target
    std::set<std::string> _gltfAllTargets;                // Valid and invalid animated targets for gltf Rig nodes
    std::map<std::string, int> _gltfValidAnimationTargets;// Valid animated targets for gltf Rig nodes
    std::map<std::string, int> _gltfMorphTargets;         // Animated targets for gltf Morph nodes
    std::map<std::string, int> _gltfMaterials;
    std::map<std::string, int> _gltfTextures;
    std::map<int, osg::Matrix> _gltfStackedMatrices;      // Stacked matrix transform for an Animation Target (node ID)
    std::set<int> _materialsWithTextures;                 // Save if current material has an assigned texture to it
    std::map<int, int> _statesetGltfMaterial;             // Save stateset UniqueID -> gltf Material ID

    // Keeps track of morph target times and weights
    std::map<float, std::map<std::string, float>> _morphTargetTimeWeights;
    std::vector<std::string> _currentMorphTargets;

    enum class MaterialSurfaceLayer {
        None, AmbientOcclusion, Albedo, ClearCoat, ClearCoatNormal, ClearCoatRoughness, DisplacementColor, Emissive, Metallic, NormalMap, 
        Reflection, Roughness, Specular, Sheen, SheenRoughness, Shininess, Transparency
    };

    std::set<std::string> _knownMaterialLayerNames =
    {
        "AOPBR",
        "Sheen",
        "Matcap",
        "BumpMap",
        "Opacity",
        "AlbedoPBR",
        "AlphaMask",
        "CavityPBR",
        "ClearCoat",
        "EmitColor",
        "NormalMap",
        "Anisotropy",
        "DiffusePBR",
        "SpecularF0",
        "SpecularPBR",
        "DiffuseColor",
        "Displacement",
        "MetalnessPBR",
        "RoughnessPBR",
        "GlossinessPBR",
        "SpecularColor",
        "SheenRoughness",
        "DiffuseIntensity",
        "SpecularHardness",
        "ClearCoatNormalMap",
        "ClearCoatRoughness",
        "SubsurfaceScattering",
        "SubsurfaceTranslucency",
    };


    void push(tinygltf::Node& gnode)
    {
        _gltfNodeStack.push(&gnode);
    }

    void pop()
    {
        _gltfNodeStack.pop();
    }

    bool pushStateSet(osg::StateSet* stateSet)
    {
        osg::Texture* osgTexture = dynamic_cast<osg::Texture*>(stateSet->getTextureAttribute(0, osg::StateAttribute::TEXTURE));
        if (!osgTexture)
        {
            return false;
        }

        _ssStack.push_back(stateSet);
        return true;
    }

    void popStateSet()
    {
        _ssStack.pop_back();
    }


    int getOrCreateBuffer(const osg::BufferData* data, GLenum type);

    int getOrCreateBufferView(const osg::BufferData* data, GLenum type, GLenum target);

    int getOrCreateGeometryAccessor(const osg::Array* data, osg::PrimitiveSet* pset, tinygltf::Primitive& prim, const std::string& attr);

    int createBindMatrixAccessor(const BindMatrices& matrix, int componentType = TINYGLTF_COMPONENT_TYPE_FLOAT);

    int getOrCreateAccessor(const osg::Array* data, int numElements, int componentType = TINYGLTF_PARAMETER_TYPE_FLOAT,
        int accessorType = TINYGLTF_TYPE_SCALAR, int bufferTarget = TINYGLTF_TARGET_ARRAY_BUFFER);


    int findBoneId(const std::string& boneName, const BoneIDNames& boneIdMap);

    osg::ref_ptr<osg::FloatArray> convertMatricesToFloatArray(const BindMatrices& matrix);

    void BuildSkinWeights(const RiggedMeshStack& rigStack, const BoneIDNames& gltfBoneIDNames);

    void getOrphanedChildren(osg::Node* childNode, std::vector<osg::Node*>& output, bool getMatrix = false);

    bool isMatrixAnimated(const osg::MatrixTransform* node);

    void createMorphTargets(const osg::Geometry* geometry, tinygltf::Mesh& mesh, int meshNodeId, bool isRigMorph, osg::ref_ptr<const osg::Vec3Array> originalVertices);

    void createVec3Sampler(tinygltf::Animation& gltfAnimation, int targetId, osgAnimation::Vec3LinearChannel* vec3Channel);

    void createQuatSampler(tinygltf::Animation& gltfAnimation, int targetId, osgAnimation::QuatSphericalLinearChannel* quatChannel);

    void gatherFloatKeys(osgAnimation::FloatLinearChannel* floatChannel, const std::string& morphTarget);

    void flushWeightsKeySampler(tinygltf::Animation& gltfAnimation, int targetId);

    void createAnimation(const osg::ref_ptr<osgAnimation::Animation> osgAnimation);

    void applyBasicAnimation(const osg::ref_ptr<osg::Callback>& callback);

    void addAnimationTarget(int gltfNodeId, const osg::ref_ptr<osg::Callback>& nodeCallback);

    void addDummyTarget(const osg::ref_ptr<osg::Callback>& nodeCallback);

    OSGtoGLTF::MaterialSurfaceLayer getTexMaterialLayer(const osg::Material* material, const osg::Texture* texture);

    int createTexture(const osg::Texture* texture, const std::string& filenameOverride = "");

    int getCurrentMaterial(osg::Geometry* geometry);

    int createTextureV2(const osgJSONParser::TextureInfo2& texInfo, const std::string& textureNameOverride = "");

    int getCurrentMaterialV2(osg::Geometry* geometry);

    int createGltfMaterialFromStateset(osg::ref_ptr<osg::StateSet> stateSet, const std::string& materialName);

    int createGltfMaterialV2(const osgJSONParser::MaterialInfo2& materialInfo);

public:
    OSGtoGLTF(tinygltf::Model& model) :
        _model(model), _firstMatrix(true), _firstNamedMatrix(true), _firstMatrixNode(nullptr)
    {
        setTraversalMode(TRAVERSE_ALL_CHILDREN);
        _model.scenes.push_back(tinygltf::Scene());
        _model.defaultScene = 0;
    }

    void apply(osg::Node& node);

    void apply(osg::Group& group);

    void apply(osg::Transform& xform);

    void apply(osg::Geometry& geometry);

    void buildAnimationTargets(osg::Group* node);
    
    bool hasTransformMatrix(const osg::Node* object);
};


