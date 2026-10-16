// The engine for Node, finding latex.wasm and latex.data beside this file:
// Emscripten's loader looks for latex.data where the program runs instead.
const path = require("node:path");

const Latex = require("./latex.js");

module.exports = (options) => Latex({ locateFile: (name) => path.join(__dirname, name), ...options });
