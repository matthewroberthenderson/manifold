# Manifold (WASM / Raycaster)
[live demo] (https://matthewroberthenderson.github.io/manifold/)
Good fun to motivate me learning the pros and cons of WebAssembly in 2026.
Last time i tried this i did **NOT** have a good time. Was not so bad this time round.

I really didn't want to use a heavy level editor, so I built a parser in the UI that reads a text string.
* `|` and `_` create the walls.
* `+` sets your starting position.
* `O` designates other player spawn points.

### Getting it running
If you want to poke around the code:
1. Make sure you have the [Emscripten SDK](https://emscripten.org/) installed.
2. Build the C++ code to WebAssembly using the provided `emcc` commands.
3. Fire up the React app with `npm start`.
