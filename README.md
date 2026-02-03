# Operating system development project
> This is a work in progress.

## Adding an embedded resource
1. Add the file to the `data/` folder
2. Create prototypes for a byte array of the file (`unsigned char`) and file length (`unsigned int`) with storage specifiers `extern const` inside `resource.h`. Variables should be formatted like`<filename>` and `<filename>_len` - for example, if the file was text.txt, the variables should be `extern const unsigned char text_txt[];` and `extern const unsigned int text_txt_len;`
3. Compile the project to generate `.c` counterparts for the declarations

## Requirements:
- xorriso
- binutils
- make
- limine
- QEMU for emulating
