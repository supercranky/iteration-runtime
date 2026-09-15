//import message from "./module";
import * as Runtime from "runtime";
import * as os from "os";

let Engine = {};

const wasmEncodeText = (text) => {
    const encoded = unescape(encodeURIComponent(text));
    const bytes = new Uint8Array(encoded.length);
    for (let i = 0; i < encoded.length; i++) bytes[i] = encoded.charCodeAt(i);
    return bytes;
};
const wasmDecodeText = (bytes) => {
    let encoded = "";
    for (let i = 0; i < bytes.length; i++) encoded += String.fromCharCode(bytes[i]);
    return decodeURIComponent(escape(encoded));
};

const encodeWasmArguments = (args) => {
    const values = args.map((value) => {
        if (value === undefined) return { tag: 0, bytes: new Uint8Array(0) };
        if (value === null) return { tag: 1, bytes: new Uint8Array(0) };
        if (typeof value === "boolean") return { tag: 2, bytes: Uint8Array.of(value ? 1 : 0) };
        if (typeof value === "number") {
            const bytes = new Uint8Array(8);
            new DataView(bytes.buffer).setFloat64(0, value, true);
            return { tag: 3, bytes };
        }
        if (typeof value === "string") return { tag: 4, bytes: wasmEncodeText(value) };
        if (value instanceof ArrayBuffer) return { tag: 5, bytes: new Uint8Array(value) };
        if (ArrayBuffer.isView(value)) {
            return { tag: 5, bytes: new Uint8Array(value.buffer, value.byteOffset, value.byteLength) };
        }
        if (Array.isArray(value)) {
            const bytes = new Uint8Array(value.length * 8);
            const view = new DataView(bytes.buffer);
            value.forEach((entry, index) => view.setFloat64(index * 8, Number(entry), true));
            return { tag: 6, bytes };
        }
        throw new TypeError("unsupported WebAssembly plugin argument");
    });
    let size = 4;
    for (const value of values) size += 8 + value.bytes.byteLength;
    const packet = new ArrayBuffer(size);
    const view = new DataView(packet);
    const output = new Uint8Array(packet);
    view.setUint32(0, values.length, true);
    let offset = 4;
    for (const value of values) {
        view.setUint32(offset, value.tag, true);
        view.setUint32(offset + 4, value.bytes.byteLength, true);
        output.set(value.bytes, offset + 8);
        offset += 8 + value.bytes.byteLength;
    }
    return packet;
};

const decodeWasmResult = (packet) => {
    if (!packet || packet.byteLength < 8) return undefined;
    const view = new DataView(packet);
    const bytes = new Uint8Array(packet);
    const tag = view.getUint32(0, true);
    const length = view.getUint32(4, true);
    if (length > packet.byteLength - 8) throw new Error("invalid WebAssembly plugin result");
    if (tag === 0) return undefined;
    if (tag === 1) return null;
    if (tag === 2 && length === 1) return bytes[8] !== 0;
    if (tag === 3 && length === 8) return view.getFloat64(8, true);
    if (tag === 4) return wasmDecodeText(bytes.slice(8, 8 + length));
    if (tag === 5) return bytes.slice(8, 8 + length);
    if (tag === 6) {
        if (length % 8) throw new Error("invalid numeric array result");
        const result = [];
        for (let offset = 8; offset < 8 + length; offset += 8)
            result.push(view.getFloat64(offset, true));
        return result;
    }
    throw new Error("unknown WebAssembly plugin result type");
};

const wasmModules = new Map();
Engine.loadWasm = (name) => {
    if (wasmModules.has(name)) return wasmModules.get(name);
    const loading = new Promise((resolve, reject) => {
    Runtime.loadWasm(name, (descriptor) => {
        if (descriptor.error) {
            reject(new Error(descriptor.error));
            return;
        }
        try {
            const manifest = JSON.parse(descriptor.manifest);
            if (manifest.abi !== "iteration.plugin/1" || !manifest.exports)
                throw new Error("invalid Iteration plugin manifest");
            const module = {};
            for (const exportName of Object.keys(manifest.exports)) {
                const method = manifest.exports[exportName];
                if (!method || !Number.isInteger(method.id) || method.id < 0)
                    throw new Error("invalid WebAssembly plugin export");
                module[exportName] = (...args) => decodeWasmResult(
                    Runtime.callWasm(descriptor.id, method.id, encodeWasmArguments(args))
                );
            }
            Object.defineProperty(module, "name", { value: manifest.name || name });
            resolve(module);
        } catch (error) {
            reject(error);
        }
    });
    });
    wasmModules.set(name, loading);
    loading.catch(() => wasmModules.delete(name));
    return loading;
};

Engine.loadTexture = (file) => {
    return new Promise((resolve) => {
        Runtime.loadTexture(file, (texture) => {
            console.log("got this", JSON.stringify(texture));
            resolve(texture);
        });
    });
};

Engine.loadTextures = async (textureFiles) => {
    let textures = {};

    let textureLoaders = textureFiles.map((file) => Engine.loadTexture(file));

    let loadedTextures = await Promise.all(textureLoaders);
    for (let texture of loadedTextures) {
        textures[texture.name] = texture.id;
    }

    /*for (let filename of textureFiles) {
        let texture = await Engine.loadTexture(filename);
        textures[texture.name] = texture.id;
    }*/

    return textures;
};

Engine.loadSound = (file) => {
    return new Promise((resolve) => {
        Runtime.loadSound(file, (sound) => {
            resolve(sound);
        });
    });
};

Engine.loadSounds = async (soundFiles) => {
    let sounds = {};

    let soundLoaders = soundFiles.map((file) => Engine.loadSound(file));
    let loadedSounds = await Promise.all(soundLoaders);

    for (let sound of loadedSounds) {
        sounds[sound.name] = sound.id;
    }

    return sounds;
};

Engine.loadFont = (file) => {
    return new Promise((resolve) => {
        Runtime.loadFont(file, (font) => {
            resolve(font);
        });
    });
};

Engine.loadText = (file) => {
    return new Promise((resolve) => {
        Runtime.loadText(file, (data) => {
            resolve(data);
        });
    });
};

Engine.loadTextureAtlas = async (jsonFile) => {
    let data = await Engine.loadText(jsonFile);
    let atlas = { ...JSON.parse(data) };
    atlas.texture = await Engine.loadTexture(atlas.meta.image);
    return atlas;
};

let arot = 0;

Engine.drawAtlasFrame = (atlas, frameName, x, y) => {
    let frame = atlas.frames[frameName].frame;

    Runtime.setTexture(atlas.texture.id);
    Runtime.drawTextureClip(
        frame.x,
        frame.y,
        frame.w,
        frame.h,
        x,
        y,
        0,
        0,
        arot,
        2,
        1
    );
    arot += 0.01;
};

console.log("this is from a loaded file");

let loadTextures = [
    "star.png",
    "slug.png",
    "small-line.png",
    "screen1.png",
    "screen2.png",
];
let textures = {};
let atlas = null;

let loadSounds = ["hey.wav"];
let sounds = {};

let scale = 0;
let angle = 0;

let x = [];
let y = [];
let z = [];
let rot = [];
let rotSpeed = [];

let type = [];
let size = 10;
let count = 2000;
let dist = 200;
let offsetX = 0;
let offsetY = 0;

let buttonClicked = false;
let buttonX = 0;
let buttonY = Runtime.getScreenBottom() - 110;
let buttonWidth = 150;
let buttonHeight = 100;
let showText = true;

let init = () => {
    console.log("running init");
    for (let i = 0; i < count; i++) {
        x[i] = -size + Math.random() * size * 2;
        y[i] = -size + Math.random() * size * 2;
        z[i] = Math.random() * dist;
        rot[i] = 0;
        rotSpeed[i] = -0.1 + Math.random() * 0.2;
        type[i] = textures["star.png"];
        if (Math.random() > 0.5) {
            //type[i] = 1;
        }
    }

    let pers = 10000;
    let update = () => {
        let currentTexture = -1;

        //Runtime.setClearColor(1.0, 0, 0, 1);

        for (let i = 0; i < count; i++) {
            if (type[i] != currentTexture) {
                currentTexture = type[i];
                Runtime.setTexture(currentTexture);
            }

            z[i] -= 1;

            rot[i] += rotSpeed[i];

            let curZ = z[i];
            let curSize = (dist - curZ) / dist;

            Runtime.drawTexture(
                (x[i] / curZ) * pers + offsetX,
                (y[i] / curZ) * pers + offsetY,
                0,
                0,
                rot[i],
                curSize * 0.5,
                curSize
            );
            if (curZ < 0) {
                z[i] = dist;
                x[i] = -size + Math.random() * size * 2;
                y[i] = -size + Math.random() * size * 2;
            }
        }
        //angle = 0.5;
        angle += 0.01;

        Runtime.setTexture(textures["small-line.png"]);
        Runtime.drawTexture(Runtime.getScreenLeft(), 0, -1, 0, 0, 1, 1);

        Runtime.setTexture(textures["screen1.png"]);
        Runtime.drawTexture(Runtime.getScreenRight(), 0, 1, 0, 0, 1, 1);

        Runtime.setTexture(textures["screen2.png"]);
        Runtime.drawTexture(0, Runtime.getScreenTop(), 0, 1, 0, 1, 1);

        Runtime.setClearColor(1.0, 0, 0, 1);

        if (showText) {
            Runtime.graphicsFontSize(20);
            Runtime.graphicsFontFace("roboto-bold.ttf");
            Runtime.graphicsFillColor(1, 1, 1, 1);
            Runtime.graphicsText(
                Runtime.getScreenLeft() + 10,
                Runtime.getScreenBottom() - 30,
                "Yo man"
            );
        }

        let color = 1;
        if (buttonClicked) {
            color = 0.5;
        }

        Runtime.graphicsBeginPath();
        Runtime.graphicsRoundedRect(
            buttonX,
            buttonY,
            buttonWidth,
            buttonHeight,
            20
        );
        Runtime.graphicsFillColor(1, 0, 0, color);
        Runtime.graphicsFill();

        //Engine.drawAtlasFrame(atlas, "cop.png", 0, 0);

        //Runtime.flushRendering();
    };

    Runtime.setOnFrame(update);

    Runtime.setOnKeyDown((event) => {
        console.log("KEY IS DOWN! code " + event.keyCode);
    });

    Runtime.setOnTouchMove((event) => {
        offsetX = -event.x;
        offsetY = -event.y;
        Runtime.setTexture(textures["star.png"]);

        Runtime.drawTexture(event.x, event.y, 0, 0, angle, 1, 1);
    });

    Runtime.setOnMouseMove((event) => {
        offsetX = -event.x;
        offsetY = -event.y;
        Runtime.setTexture(textures["star.png"]);

        Runtime.drawTexture(event.x, event.y, 0, 0, angle, 1, 1);
    });

    Runtime.setOnTouchStart((event) => {
        console.log("touch start");
        Runtime.playSound(sounds["hey.mp3"], 1, 0.8 + Math.random() * 0.4);
    });

    Runtime.setOnMouseUp((event) => {
        buttonClicked = false;
    });

    Runtime.setOnMouseDown((event) => {
        console.log("mouse down");
        Runtime.playSound(sounds["hey.mp3"], 1, 0.8 + Math.random() * 0.4);

        if (
            event.x > buttonX &&
            event.x < buttonX + buttonWidth &&
            event.y > buttonY &&
            event.y < buttonY + buttonHeight
        ) {
            buttonClicked = true;
            showText = !showText;
        }
    });
};

let run = async () => {
    textures = await Engine.loadTextures(loadTextures);
    sounds = await Engine.loadSounds(loadSounds);
    await Engine.loadFont("roboto-bold.ttf");
    atlas = await Engine.loadTextureAtlas("sprite.json");
    init();
};

run();

os.setTimeout(() => {
    console.log("this is run after a while");
}, 5000);

os.setTimeout(() => {
    console.log("even this");
}, 6000);
