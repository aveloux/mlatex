// The engine for Node, finding latex.wasm and latex.data beside this file:
// Emscripten's loader looks for latex.data where the program runs instead.
const path = require("node:path");

const Latex = require("./latex.js");

const engine = (options) => Latex({ locateFile: (name) => path.join(__dirname, name), ...options });
// The VERSION file's, which CMake writes into package.json and the release packs.
engine.version = require("./package.json").version;

module.exports = engine;
