# OSGJS GLTF/FBX Exporter

## DISCLAIMER

This software was made for educational purposes only. If you are a student or enthusiast ainimg to improve, feel free to use it. Otherwise, making models requires a good amount of time, talent and effort, and such are to be rewarded. So, please, buy the models you'll effectively use and support our 3D artists.

Also, this software is provided 'as-is', that means no warranties whatsoever. This is a WIP. And also, you shall not pay for it. It is free software, no one should be selling it!

## INSTRUCTIONS

1. What is it?

- This is an application optimized to convert Sketchfab's `.osgjs` files to `.gltf` or `fbx`, including models, colors, textures, rigging, morphing and animations. The [b]recommended[/b] format is glTF, because the App algorithm is more robust, newer and faster.

- One note is that this application is capable of reading and understanging Sketchfab specific files, such as `model_info.json` and also it may read animations and textures inside the default export directories and without any need to edit `file.osgjs` and replace the `.bin.gz` extensions - but it still needs the uncompressed files. The application will tell if any file for the model is missing or compressed.

- Also, included on this package is a Tampermonkey script to download Sketchfab textures (tampermonkey extension for Chrome avaliable [here](https://chromewebstore.google.com/detail/tampermonkey/dhdgffkkebhmkfjojejmpbldmpobfkfo?hl=pt-BR). In this script, model exportation is disabled, since it is intended to work with the original .osgjs, not a .obj file (that doesn't support rigging and animations). So use it to grab textures and place either on `textures` folder or directly into your model's folder. 

This script may download "duplicate" textures. But they are not actually duplicate. This happens because Sketchfab separate some texture channels (like Alpha Opacity) on a different texture with the same name. So, all "duplicate" textures are followed by a number at the end of file. Just open them to certify which part of the texture that channel is and adjust the names accordingly.

The application will detect any missing texture files if those metadata files are avaliable and inform you. Notice however that material processing is experimental. Sketchfab uses a custom material system that is for now impossible to replicate due limitations on both file formats. So Material importing may work for just some models, but at least some textures may be placed on a right channel and it is the easiest part to fix. If your UVs are inverted, there is an option `-O FlipUVs` to fix them.


2. Usage

After unpacking the binaries and placing the commands on your system path, go to the file's directory (prefered) and type the following command:

```
osgconv file.osgjs file-export.gltf
```

- To see all OSGJS import options type:
```
osgconv --format osgjs
```

- To see all GLTF export options type:
```
osgconv --format gltf
```

- To see all FBX export options type:
```
osgconv --format fbx
```

- Options must be used with the `-O` flag. Example: `osgconv file.osgjs file-export.gltf -O option1 [-O option2..., etc]`

- There are certain options that requires a parameter and a value, like: 
```
osgconv file.osgjs file-export.fbx -O RotateXAxis=-90.0
```
(this will tell the FBX exporter to rotate meshes in -90.0 degrees on X axis before exporting). Or:
```
osgconv file.osgjs file-export.fbx -O ScaleModel=100
```

You can also ignore rigging and animations while exporting (options `-O NoRigging` and `-O NoAnimations`) - NoRigging is for FBX only. If you are having issues with rigging or animations distorting your model, use them. `-O NoRigging` won't export rigging as Skeleton, but it WILL reconstruct all bone nodes as normal "group" nodes (visible in Blender like a series of black dots in the air - just click the dots and you'll select the corresponding group), so you can still visualize how the model's skeleton would be like before distorting model. `-O NoAnimations` is self explanatory.

### Extra tip:

If you are working with glTF, to check the model, just go to https://gltf-viewer.donmccurdy.com/, pick up your file along with the textures folder and just drag/drop them on that page.

If you are working with FBX, and for some models you get inconsistent results while importing to Blender, try opening it on FBXReview or other Autodesk product (like 3ds Max or Maya) to see if the problem is with the export or the application.

### Extra tip 2:

I put a convenience Batch file along with the exe called `OsgConvAll.bat`. With it you can convert models in batch. Just run it where your root models directory (don't use spaces for folder names) is and pass any parameters along with it if it applies. It will output everything as glTF.


That's all I can say for now.

Bye. :)