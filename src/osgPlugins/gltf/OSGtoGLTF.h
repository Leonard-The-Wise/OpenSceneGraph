
#include "pch.h"

//! Visitor that builds a GLTF data model from an OSG scene graph.
class OSGtoGLTF : public osg::NodeVisitor
{


public:
    OSGtoGLTF(tinygltf::Model& model) : _model(model), _firstMatrix(true)
    {
        setTraversalMode(TRAVERSE_ALL_CHILDREN);
        _model.scenes.push_back(tinygltf::Scene());
        _model.defaultScene = 0;
    }
    void apply(osg::Node& node);

    void apply(osg::Group& group);

    void apply(osg::Transform& xform);

    void apply(osg::Geometry& geometry);


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
    osg::Node* _firstMatrixNode;

    std::stack<std::pair<int, tinygltf::Skin*>> _gltfSkeletons;
    BindMatrices _skeletonInvBindMatrices;
    BoneIDNames _gltfBoneIDNames;


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

    int findBoneId(const std::string& boneName, const BoneIDNames& boneIdMap);

    void BuildSkinWeights(const RiggedMeshStack& rigStack, const BoneIDNames& gltfBoneIDNames);

    // template <typename T>
    // osg::ref_ptr<T> doubleToFloatArray(const osg::Array* array);

    // unsigned getBytesInDataType(GLenum dataType);

    // unsigned getBytesPerElement(const osg::Array* data);

    // unsigned getBytesPerElement(const osg::DrawElements* data);

    osg::ref_ptr<osg::FloatArray> convertMatricesToFloatArray(const BindMatrices& matrix);

    int getOrCreateBuffer(const osg::BufferData* data, GLenum type);

    int getOrCreateBufferView(const osg::BufferData* data, GLenum type, GLenum target);

    int getOrCreateGeometryAccessor(const osg::Array* data, osg::PrimitiveSet* pset, tinygltf::Primitive& prim, const std::string& attr);

    int createBindMatrixAccessor(const BindMatrices& matrix, int componentType = TINYGLTF_COMPONENT_TYPE_FLOAT);

    int getOrCreateAccessor(const osg::Array* data, int numElements, int componentType = TINYGLTF_PARAMETER_TYPE_FLOAT,
        int accessorType = TINYGLTF_TYPE_SCALAR, int bufferTarget = TINYGLTF_TARGET_ARRAY_BUFFER);

    int getCurrentMaterial();


};
