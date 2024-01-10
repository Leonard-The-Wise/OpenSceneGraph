#ifndef _READERWRITERJSON_H_
#define _READERWRITERJSON_H_

using json = nlohmann::json;

class ReaderWriterJSON : public osgDB::ReaderWriter
{
public:

    struct OptionsStruct {
        int resizeTextureUpToPowerOf2;
        bool useExternalBinaryArray;
        bool mergeAllBinaryFiles;
        bool disableCompactBuffer;
        bool inlineImages;
        bool varint;
        bool strictJson;
        bool disableVertexDecompress;
        bool rebuildMaterials;
        bool ignoreGzExtension;
        bool useTimeHack;
        bool decodeTextures;
        bool decodeTexturesNoSave;
        std::vector<std::string> useSpecificBuffer;
        std::set<std::string> additionalSourceDirs;
        std::string baseLodURL;
        std::string baseFilePath;
        OptionsStruct() {
            resizeTextureUpToPowerOf2 = 0;
            useExternalBinaryArray = false;
            mergeAllBinaryFiles = false;
            disableCompactBuffer = false;
            inlineImages = false;
            varint = false;
            strictJson = true;
            disableVertexDecompress = false;
            rebuildMaterials = false;
            ignoreGzExtension = true;
            useTimeHack = false;
            decodeTextures = false;
            decodeTexturesNoSave = false;
        }
    };


    ReaderWriterJSON()
    {
        supportsExtension("osgjs", "OpenSceneGraph Javascript implementation format");
        supportsOption("resizeTextureUpToPowerOf2=<int>", "(write option) Specify the maximum power of 2 allowed dimension for texture. Using 0 will disable the functionality and no image resizing will occur.");
        supportsOption("useExternalBinaryArray", "(write option) create binary files for vertex arrays");
        supportsOption("mergeAllBinaryFiles", "(write option) merge all binary files into one to avoid multi request on a server");
        supportsOption("inlineImages", "(write option) insert base64 encoded images instead of referring to them");
        supportsOption("varint", "(write option) Use varint encoding to serialize integer buffers");
        supportsOption("useSpecificBuffer=userkey1[=uservalue1][:buffername1],userkey2[=uservalue2][:buffername2]",
            "(write option) uses specific buffers for unshared buffers attached to geometries having a specified user key/value. Buffer name *may* be specified after ':' and will be set to uservalue by default. If no value is set then only the existence of a uservalue with key string is performed.");
        supportsOption("disableCompactBuffer", "(write option) keep source types and do not try to optimize buffers size");
        supportsOption("disableStrictJson", "(write option) do not clean string (to utf8) or floating point (should be finite) values");
        supportsOption("disableVertexDecompress", "(read option) specify to not try to decompress vertex arrays. Use this only if export fails or you get weird geometry results.");
        supportsOption("useTimeHack", "(read option) Hack animation times for compressed vectors. Using this may fix animations (not guaranteed).");
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