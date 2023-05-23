# neonbtl-wasm
NeonBTL — Neon Back to Life! emulator, WASM version.

*NeonBTL* is cross-platform NEON emulator for Windows/Linux/Mac OS X.

Soyuz-Neon is Soviet computer based on the N1806VM2 processor, so it is partially compatible with machines such as DVK, UKNC, NEMIGA, and in general inherits the instruction set and architecture from the DEC PDP-11 line of machines.

This is NeonBTL emulator version for WebAssembly (WASM) to compile with Emscripten and to run in any modern browser.

Take a look at the emulator here:
https://nzeemin.github.io/neonbtl-online.html

### Emulator files
This is the files needed to run the emulator, the following files are the result of the compilation, plus the static keyboard image:
* `emul.js`
* `emul.wasm`
* `emul.html`
* `index.html`
* `keyboard.png`

To make it work you have to put the files on web server; WebAssembly will not work just from a file opened in a browser.

### Emulator URL parameters
The emulator recognizes and uses the following (optional) URL parameters:
* `state=URL` — load saved emulator state (.neonst file) from the URL and apply it
* `diskN=URL` — load disk image (.dsk file) from the URL and attach it; `N`=0..1
* `run=1` — run the emulator

Note that the URLs are to download files from the Web by JavaScript code, so that's under restriction of Cross-Origin Resource Sharing (CORS) policy defined on your server.
