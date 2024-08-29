SET(TARGET_H
    pch.h
    json.hpp
    MViewFile.h
    MViewReader.h
    ParserHelper.h
)

SET(TARGET_SRC
    pch.cpp
    ReaderWriterMViewer.cpp
    MViewFile.cpp
    MViewReader.cpp
    ParserHelper.cpp
    
)

SET(TARGET_ADDED_LIBRARIES osgAnimation)
#### end var setup  ###
SETUP_PLUGIN(mview)
