# OSGJS-FBX Exporter

## DISCLAIMER

This plugin was made for educational purposes only. If you are a student or enthusiast ainimg to improve, feel free to use this plugin. Otherwise, I do NOT endorse piracy. Making models requires a good amount of time, talent and effort, and such are to be rewarded. So, please, buy the models you'll effectively use and support your 3D artists.

Also, this software is provided 'as-is', that means no warranties whatsoever. This is a WIP. And also, you shall not pay for it. It is free software, no one should be selling it.

## INSTRUCTIONS

1. What is it?
- These are a couple of 2 modified plugins for OpenSceneGraph (https://www.openscenegraph.com/). One is an improved version of OSGJS Reader/Writer, the other is an improved version of the FBX Reader/Writer. Both are optimized to convert Sketchfab's `.osgjs` files to `.fbx` or at least to properly read the models to use in OpenSceneGraph applications, including models, colors, textures, rigging, morphing and animations, downloaded by the `Sketchfab Ripper V 1.18`. Also, with these plugins is provived one  modified `osgconv.exe` application to work with multiple -O flags (original OpenSceneGraph osgconv.exe has a bug and it is only supporting one -O flag).

- To use this plugin you must have a valid OpenSceneGraph application, like osgconv.exe (or download my pre-made package). Once you get the OpenSceneGraph package, replace the osgjs and fbx plugins with the ones on this pack (files are usually inside `$OpenSceneGraphDir$\bin\osgPlugins-3.7.0`), optionally replace your `osgconv.exe` with mine, add the commands to your `$PATH$` environment variable and you are ready to go.

- One notice is that this plugin is capable of reading and understanging Sketchfab Ripper specific files, such as `model_info.json` and `materialInfo.txt` and also it may read animations and textures inside the default export directories and without any need to edit `file.osgjs` and replance the `.bin.gz` extensions - but it still needs the uncompressed files anyway. So, to use it properly, uncheck any conversion that Sketchfab Ripper does and just let it download/unzip files to their proper folders and then run the command manually on them after. The plugin will tell if any file for the model is missing or compressed.

- Also, I did notice that Sketchfab Ripper (at least V 1.18) is downloading broken textures, so included on this package is a Tampermonkey script to download Sketchfab textures (tampermonkey extension for Chrome avaliable [here](https://chromewebstore.google.com/detail/tampermonkey/dhdgffkkebhmkfjojejmpbldmpobfkfo?hl=pt-BR). In this script, model exportation is disabled, since it is intended to work with the original .osgjs, not a .obj file (that doesn't support rigging and animations). So use it to grab textures and place either on `textures` folder or directly into your model's folder. 

If some texture is missing from the model, it may be because of same texture names for different objects. So, I also added another version of the script that download "duplicate" textures. All duplicate textures are followed by a number at the end of file. Just open them to certify if it is duplicated or a unique one and rename it according to what materialInfo.txt provides for that model part (mesh).

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

In rare cases you may get an "out of bound indexes or vertexes", that usually means your model isn't vertex compressed (this was actually the default for OSGJS original format, but not for sketchfab's format). Since we can't programatically determine whether this model is compressed or not (depends on which version of sketchfab processor it was uploaded and time of upload), you must try different options manually.

Defaultly, the plugin will try to decompress vertices and texcoords because most recent models use it, but if it is a legacy model and it fails to export or your vertices looks totally broken, try to export with the option `-O disableVertexDecompress` and it may fix the issue.

You can also ignore rigging and animations while exporting (options `-O IgnoreRigging` and `-O IgnoreAnimations`). If you are having issues with rigging distorting your model, use it. It won't export rigging, but it WILL reconstruct all bone nodes as normal "group" nodes (visible in Blender like a series of black dots in the air - just click the dots and you'll select the corresponding group), so you can still visualize how the model's skeleton would be like before distorting model.

And also, by default we try to "compact" the OpenSceneGraph hierarchy nodes, by placing all meshes on a single Root and then applying the skeleton on it's own hierarchy. This is usually the best for most models, but with some it doesn't work. So if anything goes wrong, you can try to export with the `-O ExportFullHierarchy` option.

Finally, if everything is failing: like, your rigging won't export correctly, meshes looks weirdly rotated or distorted, you may want to use `-O ExportOriginal`, so NO PROCESSING will be done on the original hierarchy. This also disables exporting the skeleton. And also will skip animations, so you can see exactly what the scene looks like originally and make the appropriate adjust on the export parameters.

What I recommend you to do is to begin exporting models with default options, and if it fails, `-O ExportOriginal`, and then incrementally change the options later, so you'll have a baseline for which parameters to use and which to not. Most of the time, 1 try is enough but for some models, you may have to experiment with several different options to make them work.

### Extra tip:

Many times, animation data is broken (not the plugin's fault, the animation files have incorrect data), so animations may not present accurately or not play at all. So I made a hack to try and fix them, by adding custom keyframing into time arrays. Use `-O useTimeHack` on export and try it out to see if your animations are back up again. Tested with a few models only, not 100% accurate.

### Extra tip 2:

For some models I'm getting inconsistent results while importing to Blender. So use FBXReview or other Autodesk product (like 3ds Max or Maya) to see if the problem is with the export or the import plugin.

