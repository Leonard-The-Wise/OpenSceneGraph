SET(TARGET_SRC
    Animation.cpp
    Base64.cpp
    JSON_Objects.cpp
    MaterialParser.cpp
    OsgjsFileCache.cpp
    OsgjsParser.cpp
    OsgjsParserHelper.cpp
    pch.cpp
    ReaderWriterJSON.cpp
    WriteVisitor.cpp)

SET(TARGET_H
    Adaptor.h
    Animation.h
    Base64.h
    CompactBufferVisitor.h
    json.hpp
    JSON_Objects.h
    json_stream.h
    MaterialParser.h
    OsgjsFileCache.h
    OsgjsParser.h
    OsgjsParserHelper.h
    pch.h
    ReaderWriterJSON.h
    utf8_string.h
    WriteVisitor.h
)

#### end var setup  ###
SET(TARGET_ADDED_LIBRARIES
    osgAnimation
    osgSim)

SETUP_PLUGIN(osgjs)
