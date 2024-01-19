# OSGJS-FBX Exporter

## DISCLAIMER

This plugin was made for educational purposes only. If you are a student or enthusiast ainimg to improve, feel free to use this plugin. Otherwise, making models requires a good amount of time, talent and effort, and such are to be rewarded. So, please, buy the models you'll effectively use and support our 3D artists.

Also, this software is provided 'as-is', that means no warranties whatsoever. This is a WIP. And also, you shall not pay for it. It is free software, no one should be selling it!

## INSTRUCTIONS

1. What is it?
- These are a couple of modified plugins for OpenSceneGraph (https://www.openscenegraph.com/). One is an improved version of OSGJS Reader/Writer `[osgdb_osgjs.dll]`, the other is an improved version of the FBX Reader/Writer `[osgdb_fbx.dll]`. Both are optimized to convert Sketchfab's `.osgjs` files to `.fbx` or at least to properly read the models to use in OpenSceneGraph applications, including models, colors, textures, rigging, morphing and animations, downloaded by the `Sketchfab Ripper V 1.18`. Also, with these plugins is provived one modified `osgconv.exe` application to work with multiple `-O` flags (original OpenSceneGraph osgconv.exe has a bug and it is only supporting one `-O` flag).

- To use this plugin you must have a valid OpenSceneGraph application, like `osgconv.exe` (or for convenience, use my pre-made package). Once you get the OpenSceneGraph package, replace the osgjs and fbx plugins with the ones on this pack (files are usually inside `$OpenSceneGraphDir$\bin\osgPlugins-3.7.0`), optionally replace your `osgconv.exe` with mine, add the commands to your `$PATH$` environment variable and you are ready to go.

- One notice is that this plugin is capable of reading and understanging Sketchfab specific files, such as `model_info.json` and also it may read animations and textures inside the default export directories and without any need to edit `file.osgjs` and replance the `.bin.gz` extensions - but it still needs the uncompressed files. So, to use it properly, uncheck any conversion that Sketchfab Ripper does and just let it download/unzip files to their proper folders and then run the command manually on them after. The plugin will tell if any file for the model is missing or compressed.

- Also, If you are downloading with SketchRipper V 1.18, included on this package is a Tampermonkey script to download Sketchfab textures, because that version don't download correct textures (tampermonkey extension for Chrome avaliable [here](https://chromewebstore.google.com/detail/tampermonkey/dhdgffkkebhmkfjojejmpbldmpobfkfo?hl=pt-BR). In this script, model exportation is disabled, since it is intended to work with the original .osgjs, not a .obj file (that doesn't support rigging and animations). So use it to grab textures and place either on `textures` folder or directly into your model's folder. 

This script may download "duplicate" textures. But they are not actually duplicate. This happens because Sketchfab separate some texture channels (like Alpha Opacity of Albedo) on a different texture with the same name. So, all "duplicate" textures are followed by a number at the end of file. Just open them to certify which part of the texture that channel is and adjust the names accordingly.

The plugin will detect any missing texture files if those metadata files are avaliable and inform you. Notice however that material processing is experimental. Sketchfab uses a custom material system that is for now impossible to replicate in FBX format (due to FBX SDK not supporting PBR based materials). So Material importing may work for just some models, but at least some textures may be placed on a right channel and it is the easiest part to fix. If your UVs are inverted, there is an option `-O FlipUVs` to fix them.


2. Usage

After unpacking the binaries (or installing OpenSceneGraph binaries and replacing plugins) and placing the commands on your system path, use any of the OpenSceneGraph applications to work with the plugins. In this example I'm working only with the converter, so:

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

- Plugins options must be used with the `-O` flag. Example: `osgconv file.osgjs file-export.fbx -O option1 [-O option2..., etc]`

- There are certain options that requires a parameter and a value, like: 
```
osgconv file.osgjs file-export.fbx -O RotateXAxis=-90.0
```
(this will tell the FBX exporter plugin to rotate meshes in -90.0 degrees on X axis before exporting).

In rare cases you may get an "out of bound indexes or vertexes", that usually means your model isn't vertex/index compressed (this was actually the default for old OSGJS original format). Since we can't programatically determine whether this model is compressed or not (depends on which version of sketchfab processor it was uploaded and time of upload), you must try different options manually.

Defaultly, the plugin will try to decompress vertices and texcoords because most recent models use it, but if it is a legacy model and it fails to export or your vertices looks totally broken, try to export with the option `-O disableVertexDecompress` and it may fix the issue.

You can also ignore rigging and animations while exporting (options `-O IgnoreRigging` and `-O IgnoreAnimations`). If you are having issues with rigging or animations distorting your model, use it. It won't export rigging, but it WILL reconstruct all bone nodes as normal "group" nodes (visible in Blender like a series of black dots in the air - just click the dots and you'll select the corresponding group), so you can still visualize how the model's skeleton would be like before distorting model.

### Extra tip:

If for some models you get inconsistent results while importing to Blender, try opening it on FBXReview or other Autodesk product (like 3ds Max or Maya) to see if the problem is with the export or the import plugin.

### Extra tip 2:

I put a convenience Batch file along with the exe called `OsgConvAll.bat`. With it you can convert models in batch. Just run it where your root models directory is and pass any parameters along with it if it applies.


That's all I can say for now.

Cheers.