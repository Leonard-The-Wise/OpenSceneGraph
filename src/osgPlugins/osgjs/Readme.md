# OSGJS-FBX Exporter

## DISCLAIMER

This plugin was made for educational purposes only. If you are a student or enthusiast ainimg to improve, feel free to use this plugin. Otherwise, I do NOT endorse piracy. Making models requires a good amount of time, talent and effort, and such are to be rewarded. So, please, buy the models you'll effectively use and support your 3D artists.

Also, this software is provided 'as-is', that means no warranties whatsoever. This is a WIP. And also, you shall not pay for it. It is free software, no one should be selling it.

## INSTRUCTIONS

1. What is it?
- These are a couple of 2 modified plugins for OpenSceneGraph (https://www.openscenegraph.com/). One is an improved version of OSGJS Reader/Writer, the other is an improved version of the FBX Reader/Writer. Both are optimized to convert Sketchfab's `.osgjs` files to `.fbx` or at least to properly read the models to use in OpenSceneGraph applications, including models, colors, textures, rigging, morphing and animations, downloaded by the `Sketchfab Ripper V 1.18`. Also, with these plugins is provived one  modified `osgconv.exe` application to work with multiple -O flags (original OpenSceneGraph osgconv.exe only supports one -O flag).

- To use this plugin you must have a valid OpenSceneGraph application, like osgconv.exe (or download my pre-made package). Once you get the OpenSceneGraph package, replace the osgjs and fbx plugins with the ones on this pack (files are usually inside `$OpenSceneGraphDir$\bin\osgPlugins-3.7.0`), optionally replace your `osgconv.exe` with mine, add the commands to your `$PATH$` environment variable and you are ready to go.

- One notice is that this plugin is capable of reading and understanging Sketchfab Ripper specific files, such as `model_info.json` and `materialInfo.txt` and also it may read animations and textures inside the default export directories and without any need to edit `file.osgjs` and replance the `.bin.gz` extensions - but it still needs the uncompressed files anyway. So, to use it properly, uncheck any conversion that Sketchfab Ripper does and just let it download/unzip files to their proper folders and then run the command manually on them after. The plugin will tell if any file for the model is missing or compressed.

- Also, I did notice that Sketchfab Ripper (at least V 1.18) is downloading broken textures, so included on this package is a Tampermonkey script to download Sketchfab textures (tampermonkey extension for Chrome avaliable [here](https://chromewebstore.google.com/detail/tampermonkey/dhdgffkkebhmkfjojejmpbldmpobfkfo?hl=pt-BR). In this script, model exportation is disabled, since it is intended to work with the original .osgjs, not a .obj file (that doesn't support rigging and animations). So use it to grab textures and place either on `textures` folder or directly into your model's folder. This version of the script may download "duplicate" textures, but it is necessary because some textures appears with the same name for different model parts and thus the original tampermonkey script would just overwrite the texture. All duplicate textures are followed by a number at the end of file. Just open them to certify if it is duplicated or a unique one and rename it according to what materialInfo.txt provides for that model part (mesh).

The plugin will detect any missing texture files if materialInfo.txt is avaliable and inform you.


2. Usage

After installing OpenSceneGraph binaries and replacing plugins and placing the commands on path, use any of the OpenSceneGraph applications to work with the plugins. In this example I'm working only with the converter, so:

- In a command prompt, go to the file's directory (prefered) and type:
```
osgconv file.osgjs file-export.fbx
```

- To see all OSGJS Plugin options type:
```
osgconv --format osgjs
```

- To see all FBX Plugin options type:
```
osgconv --format fbx
```

- Plugins options must be used with the `-O` flag. Example: `osgconv file.osgjs file-export.fbx -O option1 [-O option2...]`

- There are certain options that requires a parameter and a value, like: 
```
osgconv file.osgjs file-export.fbx -O RotateXAxis=-90.0
```
(this will tell the FBX exporter plugin to rotate meshes in -90.0 degrees on X axis. Default rotation is 180.0, while many models uses -90.0 and some models requires rotation 90.0 or zero)

In rare cases you may get an "out of bound indexes or vertexes", that usually means your model isn't vertex compressed. Since we can't programatically determine whether this model is compressed or not (depends on which version of sketchfab processor it was uploaded and time of upload), you must try different options manually.

Defaultly, the plugin will try to decompress vertices and texcoords because most recent models use it, but if it is a legacy model and it fails to export or your vertices looks totally broken, try to export with the option `-O disableVertexDecompress` and it may fix the issue.

Also, you can ignore rigging and animations while exporting (options `-O IgnoreRigging` and `-O IgnoreAnimations`). If you are having issues with rigging, use it. 

Also, Sometimes model placement is not aligned with it's skeleton. If you don't know which rotation and your model looks weird, try to put it on Rest Pose to see which rotation it uses, or if you use the option `-O ExportOriginal`, so you won't get any deformer exported nor the model will be corrected on X-Axis (by default), but the exporter will mark all bones as normal transform groups, then you can see (eg: when opening the model in Blender) where the skeleton should be without it affecting the base model itself. After that, you will know the rotation amount you must put on `-O RotateAxis` parameter to easily realign the mesh.


### Pro tip:

If you want to see the uncompressed array data for files, just import the `.osgjs` and export to the same format (without using compression options). Just like:
```
osgconv file.osgjs file-export.osgjs
```

This way you can edit the text file (Notepad++ preferred), and see if your data has integrity or is complete (sometimes `file.osgjs` is not complete or have total integrity, hence your model may have incomplete vertices, or bones won't affect geometry properly, or animations won't play correctly, etc). 

Or just export to FBX in ASCII format (also supported by FBX Plugin).

### Pro tip 2:

Many times, animation data is broken (not the plugin's fault, the animation files have incorrect data, don't ask me why). So I made a hack to try and fix animations, by adding custom keyframing into time arrays. Use `-O useTimeHack` on export and try it out to see if your animations are back up again.