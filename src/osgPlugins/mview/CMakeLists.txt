SET(TARGET_H
    pch.h
    json.hpp
)

SET(TARGET_SRC
    pch.cpp
    ReaderWriterMViewer.cpp
)

SET(TARGET_ADDED_LIBRARIES osgAnimation)
#### end var setup  ###
SETUP_PLUGIN(mview)
