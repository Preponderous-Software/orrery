# stb_image

`stb_image.h` v2.30 by Sean Barrett, from https://github.com/nothings/stb at
commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` (unmodified; SHA-256
`594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`). Public
domain (or MIT, at the user's choice), as stated at the end of the file.
The orrery uses it only to decode the JPEG planet maps in `data/textures/`,
through `image_file.cpp`, which compiles it with `STB_IMAGE_STATIC` so its
symbols stay private to that file.
