# Operating system development project
> This is a work in progress.

## Development
### Registering exception handlers
  You can now register a custom handler like this:

```
   void my_page_fault_handler(interrupt_frame_t *frame) {
       // Do stuff
       panic("Page Fault");
   }

   // In register_exceptions():
   exception_install_handler(14, my_page_fault_handler);
```

### Adding an embedded resource
1. Add the file to the `data/` folder
2. Create prototypes for a byte array of the file (`unsigned char`) and file length (`unsigned int`) with storage specifiers `extern const` inside `resource.h`. Variables should be formatted like`<filename>` and `<filename>_len` - for example, if the file was called file.txt, the variables should be `extern const unsigned char file_txt[];` and `extern const unsigned int file_txt_len;`
3. Compile the project to generate `.c` counterparts for the declarations

## Building
To compile the kernel and cdrom, run: `make cdrom`<br>
You can also cross-compile by specifying the compiler prefix using the `CROSS-PREFIX` make argument

## Requirements:
- xorriso
- binutils
- make
- limine
- QEMU for emulating
