SET(TARGET_H
    GLTFWriter.h
    OsgtoGLTF.h
    pch.h
    Stringify.h
    tiny_gltf.h
    json.hpp
    MaterialParser.h
    MViewMaterial.h
)

SET(TARGET_SRC
    GLTFWriter.cpp
    OSGtoGLTF.cpp
    pch.cpp
    ReaderWriterGLTF.cpp
    MaterialParser.cpp
    MViewMaterial.cpp
)

SET(TARGET_ADDED_LIBRARIES
    osgAnimation
    osgSim)
#### end var setup  ###
SETUP_PLUGIN(gltf)
