Qt6 plus QML
* model-viewmodel-view architecture
* reuseable components and utilities (such as work queue)
* pimpl, dependencies should not leak


Non-GPL libraries only (mit, bsd, apache2, lgpl, ...) 

OpenCV for filters
LibTIFF (with zstd), because openCV is not suitable for multipage/very large/tiled tiffs 
LibJXL
LibJPEG (thumbnails)
zlib
zstandard for tiffs and thumbnail cache?
sqlite for thumbnail cache?
external exiftool or imagemagick allowed, some library for reading
c++23, 
should support tif, png, jpeg ... later webp, avif, jxl
nlohman::json?

Support for big tiffs, image pyramids and tiled loading should be there from start to set it apart from other viewers
Also 16bit from start. I think we can use vulkan as a backend somewhere

Multithreaded, but with possible limit (like 2/3 of cores), may use IntelTBB/OneTBB?
- operations such as thumbnail or directory scan should be non-blocking, thumbnail 
  generator should be work queue with priority to fetch thumbnails for images visible
  in gui first.

Target platforms windows and Linux, but I would say use functions optionally
- for example folder explorer may reuse places from KDE or Windows, system integration plugin?

Use conan on windows, system libraries on Linux

SVG icons, maybe primary and secondary colors, that can be replaced depending on dark/gray/ligh theme and accent color
