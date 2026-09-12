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
        0,
        2,
        1
    );
    arot += 0.01;
};

// ecs stuff

let actors = new Map();
let actorIdCount = 1;
let engines = [];

Engine.createActor = (components) => {
    components.id = actorIdCount;
    actors[actorIdCount++] = components;
};

Engine.getActorsWithComponent = (component) => {
    return Array.from(actors).filter((item) => {
        return item[component];
    });
};

let invader = Engine.createActor({
    Rect: {
        x: 10,
        y: 10,
        width: 20,
        height: 20,
    },
    Sprite: {
        name: "invader1-1.png",
    },
    Collider: {
        mask: ["Shot"],
    },
});

let spriteEngine = {
    component: "Sprite",
    update: (actors) => {
        for (let actor of actors) {
            console.log("doing sprite stuff with actor");
            // do sprite stuff
        }
    },
};

engines.push(spriteEngine);

let updateEngines = () => {
    for (let engine of engines) {
        let actors = getActorsWithComponent(engine.component);
        engine.update(actors);
    }
};

// --- game starts here

let atlas = null;

let init = () => {
    let update = () => {
        updateEngines();
    };

    Runtime.setOnFrame(update);

    Runtime.setOnKeyDown((event) => {
        console.log("KEY IS DOWN! code " + event.keyCode);
    });

    Runtime.setOnTouchMove((event) => {});

    Runtime.setOnMouseMove((event) => {});

    Runtime.setOnTouchStart((event) => {});

    Runtime.setOnMouseUp((event) => {
        buttonClicked = false;
    });

    Runtime.setOnMouseDown((event) => {});
};

let run = async () => {
    await Engine.loadFont("roboto-bold.ttf");
    atlas = await Engine.loadTextureAtlas("sprite.json");
    init();
};

run();
