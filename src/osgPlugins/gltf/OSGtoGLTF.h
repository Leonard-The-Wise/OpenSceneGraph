
#include "pch.h"

//! Visitor that builds a GLTF data model from an OSG scene graph.
class OSGtoGLTF : public osg::NodeVisitor
{
private:
    typedef std::map<osg::ref_ptr< const osg::Node >, int> OsgNodeSequenceMap;
    typedef std::map<osg::ref_ptr<const osg::BufferData>, int> ArraySequenceMap;
    typedef std::map< osg::ref_ptr<const osg::Array>, int> AccessorSequenceMap;
    typedef std::vector< osg::ref_ptr< osg::StateSet > > StateSetStack;

    std::vector< osg::ref_ptr< osg::Texture > > _textures;

    tinygltf::Model& _model;
    std::stack<tinygltf::Node*> _gltfNodeStack;
    OsgNodeSequenceMap _osgNodeSeqMap;
    ArraySequenceMap _buffers;
    ArraySequenceMap _bufferViews;
    ArraySequenceMap _accessors;
    StateSetStack _ssStack;

public:
    OSGtoGLTF(tinygltf::Model& model) : _model(model)
    {
        setTraversalMode(TRAVERSE_ALL_CHILDREN);
        setNodeMaskOverride(~0);

        // default root scene:
        _model.scenes.push_back(tinygltf::Scene());
        //tinygltf::Scene& scene = _model.scenes.back();
        _model.defaultScene = 0;
    }

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


    void apply(osg::Node& node);

    void apply(osg::Group& group);

    void apply(osg::Transform& xform);

    unsigned getBytesInDataType(GLenum dataType);

    unsigned getBytesPerElement(const osg::Array* data);

    unsigned getBytesPerElement(const osg::DrawElements* data);

    int getOrCreateBuffer(const osg::BufferData* data, GLenum type);

    int getOrCreateBufferView(const osg::BufferData* data, GLenum type, GLenum target);

    int getOrCreateAccessor(osg::Array* data, osg::PrimitiveSet* pset, tinygltf::Primitive& prim, const std::string& attr);

    int getCurrentMaterial();

    void apply(osg::Geometry& geometry);
};
