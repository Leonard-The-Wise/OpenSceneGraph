#ifndef _READERWRITERJSON_H_
#define _READERWRITERJSON_H_

using json = nlohmann::json;

class ReaderWriterJSON : public osgDB::ReaderWriter
{
public:

    struct OptionsStruct {
        int resizeTextureUpToPowerOf2;
        int applicationKey;
        bool useExternalBinaryArray;
        bool mergeAllBinaryFiles;
        bool disableCompactBuffer;
        bool inlineImages;
        bool varint;
        bool strictJson;
        bool rebuildMaterials;
        bool ignoreGzExtension;
        bool ignoreAnimations;
        std::vector<std::string> useSpecificBuffer;
        std::set<std::string> additionalSourceDirs;
        std::string baseLodURL;
        std::string baseFilePath;
        OptionsStruct() {
            resizeTextureUpToPowerOf2 = 0;
            applicationKey = 0;
            useExternalBinaryArray = false;
            mergeAllBinaryFiles = false;
            disableCompactBuffer = false;
            inlineImages = false;
            varint = false;
            strictJson = true;
            rebuildMaterials = false;
            ignoreGzExtension = true;
            ignoreAnimations = false;
        }
    };


    ReaderWriterJSON()
    {
        supportsExtension("osgjs", "OpenSceneGraph Javascript implementation format");
        supportsOption("ResizeTextureUpToPowerOf2=<int>", "(write option) Specify the maximum power of 2 allowed dimension for texture. Using 0 will disable the functionality and no image resizing will occur.");
        supportsOption("UseExternalBinaryArray", "(write option) create binary files for vertex arrays");
        supportsOption("MergeAllBinaryFiles", "(write option) merge all binary files into one to avoid multi request on a server");
        supportsOption("InlineImages", "(write option) insert base64 encoded images instead of referring to them");
        supportsOption("Varint", "(write option) Use varint encoding to serialize integer buffers");
        supportsOption("UseSpecificBuffer=userkey1[=uservalue1][:buffername1],userkey2[=uservalue2][:buffername2]",
            "(write option) uses specific buffers for unshared buffers attached to geometries having a specified user key/value. Buffer name *may* be specified after ':' and will be set to uservalue by default. If no value is set then only the existence of a uservalue with key string is performed.");
        supportsOption("DisableCompactBuffer", "(write option) keep source types and do not try to optimize buffers size");
        supportsOption("DisableStrictJson", "(write option) do not clean string (to utf8) or floating point (should be finite) values");
        supportsOption("NoAnimations", "(read option) Import model without animations.");
    }

    virtual const char* className() const { return "OSGJS json Reader/Writer"; }

    virtual ReadResult readObject(const std::string& filename, const osgDB::ReaderWriter::Options* options) const;

    virtual ReadResult readNode(const std::string& fileName, const Options* options) const;

    virtual WriteResult writeNode(const osg::Node& node,
        const std::string& fileName,
        const osgDB::ReaderWriter::Options* options) const;

    virtual WriteResult writeNode(const osg::Node& node,
        json_stream& fout,
        const osgDB::ReaderWriter::Options* options) const;

    virtual WriteResult writeNodeModel(const osg::Node& node, json_stream& fout, const std::string& basename, const OptionsStruct& options) const;

    ReaderWriterJSON::OptionsStruct parseOptions(const osgDB::ReaderWriter::Options* options) const;

    void getModelFiles(const nlohmann::json& value, std::set<std::string>& FileNames) const;

    osg::ref_ptr<osg::Node> parseOsgjs(const nlohmann::json& input, const OptionsStruct& options) const;

};

#endif