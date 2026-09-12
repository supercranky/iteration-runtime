//import message from "./module";
import * as Runtime from "runtime";
import * as os from "os";

let Engine = {};

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
